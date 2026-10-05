// SPDX-License-Identifier: GPL-3.0-only
//! Local transport boundary. No game simulation, filesystem API or general proxy.
use axum::{
    extract::{
        ws::{Message, WebSocket, WebSocketUpgrade},
        ConnectInfo, State,
    },
    http::{header, HeaderMap, StatusCode, Uri},
    response::{IntoResponse, Response},
    routing::get,
    Router,
};
use include_dir::{include_dir, Dir};
use rand::RngCore;
use serde::{Deserialize, Serialize};
use std::{
    net::{Ipv4Addr, SocketAddr},
    sync::{
        atomic::{AtomicBool, Ordering},
        Arc,
    },
    time::{Duration, Instant},
};
use subtle::ConstantTimeEq;
use tokio::{
    io::{AsyncReadExt, AsyncWriteExt},
    net::TcpStream,
};
use tokio::{
    net::TcpListener,
    sync::{OwnedSemaphorePermit, Semaphore},
    task::JoinHandle,
    time::timeout,
};
use tokio_util::{sync::CancellationToken, task::TaskTracker};

pub mod preferences;
use preferences::Preferences;

static ASSETS: Dir = include_dir!("$CARGO_MANIFEST_DIR/../dist");
const MAX_MESSAGE: usize = 8192;
const CONTROL_LIMIT: usize = 1024;
const AUTH_TIMEOUT: Duration = Duration::from_secs(3);
const IDLE_TIMEOUT: Duration = Duration::from_secs(35);

// Deliberately no Debug: credentials must not appear in diagnostics.
#[derive(Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct Bootstrap {
    pub origin: String,
    pub token: String,
    #[cfg(feature = "dev-echo")]
    pub test_port: u16,
}
struct Shared {
    bootstrap: Bootstrap,
    authority: String,
    cancel: CancellationToken,
    tasks: TaskTracker,
    slots: Arc<Semaphore>,
    authenticated: AtomicBool,
    preferences: std::sync::Mutex<Option<Preferences>>,
}
pub struct Bridge {
    pub bootstrap: Bootstrap,
    shared: Arc<Shared>,
    server: JoinHandle<std::io::Result<()>>,
}
impl Bridge {
    pub async fn start() -> std::io::Result<Self> {
        // No configurable bind address. Port 0 asks the OS for an unused port.
        let listener = TcpListener::bind((Ipv4Addr::LOCALHOST, 0)).await?;
        let authority = listener.local_addr()?.to_string();
        let mut bytes = [0u8; 32];
        rand::rngs::OsRng
            .try_fill_bytes(&mut bytes)
            .map_err(std::io::Error::other)?;
        let cancel = CancellationToken::new();
        let tasks = TaskTracker::new();
        #[cfg(feature = "dev-echo")]
        let test_port = start_echo(cancel.clone(), tasks.clone()).await?;
        let bootstrap = Bootstrap {
            origin: format!("http://{authority}"),
            token: bytes.iter().map(|b| format!("{b:02x}")).collect(),
            #[cfg(feature = "dev-echo")]
            test_port,
        };
        let shared = Arc::new(Shared {
            bootstrap: bootstrap.clone(),
            authority,
            cancel: cancel.clone(),
            tasks,
            slots: Arc::new(Semaphore::new(4)),
            authenticated: AtomicBool::new(false),
            preferences: std::sync::Mutex::new(None),
        });
        let router = Router::new()
            .route("/bridge", get(upgrade))
            .fallback(get(asset))
            .with_state(shared.clone());
        let server = tokio::spawn(async move {
            axum::serve(
                listener,
                router.into_make_service_with_connect_info::<SocketAddr>(),
            )
            .with_graceful_shutdown(cancel.cancelled_owned())
            .await
        });
        Ok(Self {
            bootstrap,
            shared,
            server,
        })
    }
    pub fn set_preferences(&self, value: Preferences) -> bool {
        if !value.valid() {
            return false;
        }
        *self.shared.preferences.lock().unwrap() = Some(value);
        true
    }
    pub fn preferences(&self) -> Option<Preferences> {
        self.shared.preferences.lock().unwrap().clone()
    }
    pub fn is_authenticated(&self) -> bool {
        self.shared.authenticated.load(Ordering::Acquire)
    }
    pub fn request_shutdown(&self) {
        self.shared.cancel.cancel();
    }
    pub async fn shutdown(self) {
        self.request_shutdown();
        self.shared.tasks.close();
        let _ = timeout(Duration::from_secs(2), self.shared.tasks.wait()).await;
        // Idle HTTP keep-alive peers must not delay application exit indefinitely.
        let mut server = self.server;
        if timeout(Duration::from_secs(2), &mut server).await.is_err() {
            server.abort();
            let _ = server.await;
        }
    }
}
fn local_request(headers: &HeaderMap, peer: SocketAddr, state: &Shared) -> bool {
    peer.ip().is_loopback()
        && headers.get(header::HOST).and_then(|v| v.to_str().ok()) == Some(state.authority.as_str())
}
async fn asset(
    State(state): State<Arc<Shared>>,
    ConnectInfo(peer): ConnectInfo<SocketAddr>,
    headers: HeaderMap,
    uri: Uri,
) -> Response {
    if !local_request(&headers, peer, &state) {
        return StatusCode::FORBIDDEN.into_response();
    }
    let path = match uri.path() {
        "/" => "index.html",
        p => p.trim_start_matches('/'),
    };
    let Some(file) = ASSETS.get_file(path) else {
        return StatusCode::NOT_FOUND.into_response();
    };
    let kind = if path.ends_with(".html") {
        "text/html; charset=utf-8"
    } else if path.ends_with(".js") {
        "text/javascript; charset=utf-8"
    } else if path.ends_with(".css") {
        "text/css; charset=utf-8"
    } else if path.ends_with(".svg") {
        "image/svg+xml"
    } else {
        "application/octet-stream"
    };
    let csp = format!("default-src 'none'; script-src 'self'; style-src 'self'; img-src 'self'; connect-src ws://{}/bridge; base-uri 'none'; form-action 'none'; frame-ancestors 'none'; object-src 'none'", state.authority);
    (
        [
            (header::CONTENT_TYPE, kind.to_owned()),
            (header::CONTENT_SECURITY_POLICY, csp),
            (header::CACHE_CONTROL, "no-store".into()),
            (header::X_CONTENT_TYPE_OPTIONS, "nosniff".into()),
            (header::REFERRER_POLICY, "no-referrer".into()),
        ],
        file.contents(),
    )
        .into_response()
}
async fn upgrade(
    State(state): State<Arc<Shared>>,
    ConnectInfo(peer): ConnectInfo<SocketAddr>,
    headers: HeaderMap,
    ws: WebSocketUpgrade,
) -> Response {
    if !local_request(&headers, peer, &state)
        || headers.get(header::ORIGIN).and_then(|v| v.to_str().ok())
            != Some(state.bootstrap.origin.as_str())
    {
        return StatusCode::FORBIDDEN.into_response();
    }
    let Ok(permit) = state.slots.clone().try_acquire_owned() else {
        return StatusCode::TOO_MANY_REQUESTS.into_response();
    };
    ws.max_message_size(MAX_MESSAGE)
        .max_frame_size(MAX_MESSAGE)
        .write_buffer_size(0)
        .max_write_buffer_size(65536)
        .on_upgrade(move |socket| async move {
            let task = state.tasks.spawn(session(socket, state.clone(), permit));
            let _ = task.await;
        })
        .into_response()
}
#[derive(Deserialize)]
#[serde(tag = "type", deny_unknown_fields)]
enum Command {
    #[serde(rename = "AUTH")]
    Auth { token: String },
    #[serde(rename = "CONNECT")]
    Connect { host: String, port: u16 },
    #[serde(rename = "DISCONNECT")]
    Disconnect {},
    #[serde(rename = "PING")]
    Ping {},
    #[serde(rename = "SETTINGS")]
    Settings { value: Preferences },
}
fn command(message: Message) -> Option<Command> {
    if let Message::Text(text) = message {
        if text.len() <= CONTROL_LIMIT {
            return serde_json::from_str(&text).ok();
        }
    }
    None
}
pub fn valid_target(host: &str, port: u16) -> bool {
    if port == 0 || host.is_empty() || host.len() > 253 || !host.is_ascii() {
        return false;
    }
    if host.parse::<std::net::IpAddr>().is_ok() {
        return true;
    }
    host.split('.').all(|s| {
        !s.is_empty()
            && s.len() <= 63
            && s.as_bytes()[0].is_ascii_alphanumeric()
            && s.as_bytes()[s.len() - 1].is_ascii_alphanumeric()
            && s.bytes().all(|b| b.is_ascii_alphanumeric() || b == b'-')
    })
}
async fn reply(socket: &mut WebSocket, value: serde_json::Value) -> bool {
    matches!(
        timeout(
            Duration::from_secs(2),
            socket.send(Message::Text(value.to_string().into()))
        )
        .await,
        Ok(Ok(()))
    )
}
async fn error(socket: &mut WebSocket, code: &str) {
    reply(socket, serde_json::json!({"type":"ERROR", "code": code})).await;
}
struct AuthGuard(Arc<Shared>);
impl Drop for AuthGuard {
    fn drop(&mut self) {
        self.0.authenticated.store(false, Ordering::Release);
    }
}
async fn session(mut socket: WebSocket, state: Arc<Shared>, _permit: OwnedSemaphorePermit) {
    let first = tokio::select! {
        _ = state.cancel.cancelled() => return,
        result = timeout(AUTH_TIMEOUT, socket.recv()) => result,
    };
    let Ok(Some(Ok(message))) = first else {
        return;
    };
    let Some(Command::Auth { token }) = command(message) else {
        error(&mut socket, "AUTH_REQUIRED").await;
        return;
    };
    if token.len() != 64 || !bool::from(token.as_bytes().ct_eq(state.bootstrap.token.as_bytes())) {
        error(&mut socket, "AUTH_FAILED").await;
        return;
    }
    if state.authenticated.swap(true, Ordering::AcqRel) {
        error(&mut socket, "CLIENT_BUSY").await;
        return;
    }
    let _auth = AuthGuard(state.clone());
    if !reply(
        &mut socket,
        serde_json::json!({"type":"AUTH_OK", "version":1}),
    )
    .await
    {
        return;
    }
    let preferences = state.preferences.lock().unwrap().clone();
    if let Some(value) = preferences {
        if !reply(
            &mut socket,
            serde_json::json!({"type":"PREFERENCES", "value":value}),
        )
        .await
        {
            return;
        }
    }
    let mut window = Instant::now();
    let mut count = 0u32;
    let mut tcp: Option<TcpStream> = None;
    let mut buffer = [0u8; MAX_MESSAGE];
    loop {
        let message = tokio::select! {
            _ = state.cancel.cancelled() => break,
            result = timeout(IDLE_TIMEOUT, socket.recv()) => match result { Ok(Some(Ok(m))) => m, _ => break },
            result = async { match tcp.as_mut() { Some(stream) => stream.read(&mut buffer).await, None => std::future::pending().await } } => {
                match result {
                    Ok(n) if n > 0 => {
                        if !matches!(timeout(Duration::from_secs(2), socket.send(Message::Binary(buffer[..n].to_vec().into()))).await, Ok(Ok(()))) { break; }
                    },
                    _ => { tcp = None; if !reply(&mut socket, serde_json::json!({"type":"DISCONNECTED"})).await { break; } }
                }
                continue;
            }
        };
        if matches!(message, Message::Close(_)) {
            break;
        }
        if window.elapsed() >= Duration::from_secs(1) {
            window = Instant::now();
            count = 0;
        }
        count += 1;
        if count > 30 {
            error(&mut socket, "RATE_LIMIT").await;
            break;
        }
        if let Message::Binary(ref bytes) = message {
            let Some(stream) = tcp.as_mut() else {
                error(&mut socket, "NOT_CONNECTED").await;
                break;
            };
            if !matches!(
                timeout(Duration::from_secs(2), stream.write_all(bytes)).await,
                Ok(Ok(()))
            ) {
                break;
            }
            continue;
        }
        match command(message) {
            Some(Command::Settings { value }) => {
                if !value.valid() {
                    error(&mut socket, "INVALID_SETTINGS").await;
                    break;
                }
                *state.preferences.lock().unwrap() = Some(value);
                if !reply(&mut socket, serde_json::json!({"type":"SETTINGS_OK"})).await {
                    break;
                }
            }
            Some(Command::Ping {}) => {
                if !reply(&mut socket, serde_json::json!({"type":"PONG"})).await {
                    break;
                }
            }
            Some(Command::Disconnect {}) => {
                tcp = None;
                if !reply(&mut socket, serde_json::json!({"type":"DISCONNECTED"})).await {
                    break;
                }
            }
            Some(Command::Connect { host, port }) => {
                if !valid_target(&host, port) {
                    error(&mut socket, "INVALID_TARGET").await;
                    break;
                }
                #[cfg(feature = "dev-echo")]
                if host == "127.0.0.1" && port == state.bootstrap.test_port && tcp.is_none() {
                    match timeout(
                        Duration::from_secs(2),
                        TcpStream::connect((Ipv4Addr::LOCALHOST, port)),
                    )
                    .await
                    {
                        Ok(Ok(stream)) => {
                            tcp = Some(stream);
                            if !reply(&mut socket, serde_json::json!({"type":"CONNECTED"})).await {
                                break;
                            }
                        }
                        _ => error(&mut socket, "CONNECT_FAILED").await,
                    }
                    continue;
                }
                // M1 deliberately has no unrestricted TCP dial path. M2 must add
                // a protocol-specific gate before enabling remote endpoints.
                error(&mut socket, "PROTOCOL_NOT_READY").await;
            }
            _ => {
                error(&mut socket, "INVALID_COMMAND").await;
                break;
            }
        }
    }
    let _ = timeout(
        Duration::from_millis(200),
        socket.send(Message::Close(None)),
    )
    .await;
}
#[cfg(feature = "dev-echo")]
async fn start_echo(cancel: CancellationToken, tasks: TaskTracker) -> std::io::Result<u16> {
    let listener = TcpListener::bind((Ipv4Addr::LOCALHOST, 0)).await?;
    let port = listener.local_addr()?.port();
    let children = tasks.clone();
    tasks.spawn(async move {
        loop {
            let accepted = tokio::select! { _ = cancel.cancelled() => break, a = listener.accept() => a };
            let Ok((mut stream, _)) = accepted else { break; };
            let stop = cancel.clone();
            children.spawn(async move {
                let (mut reader, mut writer) = stream.split();
                tokio::select! { _ = stop.cancelled() => {}, _ = tokio::io::copy(&mut reader, &mut writer) => {} }
            });
        }
    });
    Ok(port)
}
