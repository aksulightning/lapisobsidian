// SPDX-License-Identifier: GPL-3.0-only
use futures_util::{SinkExt, StreamExt};
use lapis_bridge::{valid_target, Bridge};
use serde_json::{json, Value};
use std::time::Duration;
use tokio::{
    io::{AsyncReadExt, AsyncWriteExt},
    net::TcpStream,
    time::timeout,
};
use tokio_tungstenite::{
    connect_async,
    tungstenite::{client::IntoClientRequest, Message},
    MaybeTlsStream, WebSocketStream,
};
type Socket = WebSocketStream<MaybeTlsStream<TcpStream>>;
async fn socket(bridge: &Bridge) -> Socket {
    let mut request = format!("{}/bridge", bridge.bootstrap.origin.replace("http:", "ws:"))
        .into_client_request()
        .unwrap();
    request
        .headers_mut()
        .insert("Origin", bridge.bootstrap.origin.parse().unwrap());
    connect_async(request).await.unwrap().0
}
async fn send(ws: &mut Socket, value: Value) {
    ws.send(Message::Text(value.to_string().into()))
        .await
        .unwrap();
}
async fn read(ws: &mut Socket) -> Value {
    let message = timeout(Duration::from_secs(3), ws.next())
        .await
        .unwrap()
        .unwrap()
        .unwrap();
    serde_json::from_str(message.to_text().unwrap()).unwrap()
}
async fn auth(bridge: &Bridge) -> Socket {
    let mut ws = socket(bridge).await;
    send(
        &mut ws,
        json!({"type":"AUTH","token":bridge.bootstrap.token}),
    )
    .await;
    assert_eq!(read(&mut ws).await["type"], "AUTH_OK");
    ws
}
#[test]
fn target_validation() {
    for host in [
        "localhost",
        "example.org",
        "127.0.0.1",
        "::1",
        "a-b.example",
    ] {
        assert!(valid_target(host, 25565));
    }
    for host in [
        "",
        "a/b",
        "a@b",
        "a b",
        "-a.org",
        "a-.org",
        ".a",
        "a..b",
        "a\n",
        "é.org",
        "http://example.org",
        "[::1]",
    ] {
        assert!(!valid_target(host, 25565), "{host:?}");
    }
    assert!(!valid_target("localhost", 0));
    assert!(!valid_target(&"a".repeat(254), 1));
}
#[tokio::test]
async fn lifecycle_and_per_launch_secrets() {
    let first = Bridge::start().await.unwrap();
    let second = Bridge::start().await.unwrap();
    assert_ne!(first.bootstrap.token, second.bootstrap.token);
    assert_ne!(first.bootstrap.origin, second.bootstrap.origin);
    assert!(first.bootstrap.origin.starts_with("http://127.0.0.1:"));
    assert_eq!(first.bootstrap.token.len(), 64);
    let address = first
        .bootstrap
        .origin
        .trim_start_matches("http://")
        .to_string();
    let mut ws = auth(&first).await;
    send(&mut ws, json!({"type":"PING"})).await;
    assert_eq!(read(&mut ws).await["type"], "PONG");
    first.shutdown().await;
    assert!(timeout(Duration::from_secs(1), ws.next()).await.is_ok());
    assert!(TcpStream::connect(&address).await.is_err());
    let rebound = tokio::net::TcpListener::bind(&address).await.unwrap();
    drop(rebound);
    second.shutdown().await;
}
#[tokio::test]
async fn hostile_origins_and_hosts_are_rejected() {
    let bridge = Bridge::start().await.unwrap();
    for origin in [
        None,
        Some("null"),
        Some("https://evil.example"),
        Some("http://localhost:1234"),
    ] {
        let mut request = format!("{}/bridge", bridge.bootstrap.origin.replace("http:", "ws:"))
            .into_client_request()
            .unwrap();
        if let Some(origin) = origin {
            request
                .headers_mut()
                .insert("Origin", origin.parse().unwrap());
        }
        let err = connect_async(request).await.unwrap_err();
        assert!(err.to_string().contains("403"));
    }
    let address = bridge.bootstrap.origin.trim_start_matches("http://");
    let mut stream = TcpStream::connect(address).await.unwrap();
    stream
        .write_all(b"GET / HTTP/1.1\r\nHost: evil.example\r\nConnection: close\r\n\r\n")
        .await
        .unwrap();
    let mut data = Vec::new();
    stream.read_to_end(&mut data).await.unwrap();
    assert!(String::from_utf8_lossy(&data).starts_with("HTTP/1.1 403"));
    bridge.shutdown().await;
}
#[tokio::test]
async fn no_credentials_in_assets_and_no_filesystem_routes() {
    let bridge = Bridge::start().await.unwrap();
    for (path, status) in [
        ("/", "200"),
        ("/LICENSE", "200"),
        ("/etc/passwd", "404"),
        ("/../Cargo.toml", "404"),
    ] {
        let address = bridge.bootstrap.origin.trim_start_matches("http://");
        let mut stream = TcpStream::connect(address).await.unwrap();
        stream
            .write_all(
                format!("GET {path} HTTP/1.1\r\nHost: {address}\r\nConnection: close\r\n\r\n")
                    .as_bytes(),
            )
            .await
            .unwrap();
        let mut data = Vec::new();
        stream.read_to_end(&mut data).await.unwrap();
        let response = String::from_utf8_lossy(&data);
        assert!(response.starts_with(&format!("HTTP/1.1 {status}")));
        assert!(!response.contains(&bridge.bootstrap.token));
        if status == "200" {
            assert!(response.contains("content-security-policy:"));
            assert!(response.contains("frame-ancestors 'none'"));
        }
    }
    bridge.shutdown().await;
}
#[tokio::test]
async fn authentication_and_single_client() {
    let bridge = Bridge::start().await.unwrap();
    for (command, code) in [
        (
            json!({"type":"CONNECT","host":"example.org","port":25565}),
            "AUTH_REQUIRED",
        ),
        (json!({"type":"AUTH","token":"0".repeat(64)}), "AUTH_FAILED"),
    ] {
        let mut ws = socket(&bridge).await;
        send(&mut ws, command).await;
        assert_eq!(read(&mut ws).await["code"], code);
    }
    let mut ws = auth(&bridge).await;
    let mut other = socket(&bridge).await;
    send(
        &mut other,
        json!({"type":"AUTH","token":bridge.bootstrap.token}),
    )
    .await;
    assert_eq!(read(&mut other).await["code"], "CLIENT_BUSY");
    send(
        &mut ws,
        json!({"type":"CONNECT","host":"example.org","port":25565}),
    )
    .await;
    assert_eq!(read(&mut ws).await["code"], "PROTOCOL_NOT_READY");
    bridge.shutdown().await;
}
#[tokio::test]
async fn invalid_commands_and_binary_before_connect() {
    for payload in [
        Message::Text("{".into()),
        Message::Text("{\"type\":\"PING\",\"shell\":\"no\"}".into()),
        Message::Text("{\"type\":\"CONNECT\",\"host\":\"a\",\"port\":65536}".into()),
        Message::Binary(vec![1, 2].into()),
    ] {
        let bridge = Bridge::start().await.unwrap();
        let mut ws = auth(&bridge).await;
        ws.send(payload).await.unwrap();
        let message = read(&mut ws).await;
        assert_eq!(message["type"], "ERROR");
        bridge.shutdown().await;
    }
}
#[tokio::test]
async fn oversized_messages_close_the_session() {
    let bridge = Bridge::start().await.unwrap();
    let mut ws = auth(&bridge).await;
    ws.send(Message::Text("x".repeat(8193).into()))
        .await
        .unwrap();
    let reply = timeout(Duration::from_secs(3), ws.next()).await.unwrap();
    assert!(!matches!(reply, Some(Ok(Message::Text(_)))));
    bridge.shutdown().await;
}
#[tokio::test]
async fn command_rate_is_bounded() {
    let bridge = Bridge::start().await.unwrap();
    let mut ws = auth(&bridge).await;
    for _ in 0..30 {
        send(&mut ws, json!({"type":"PING"})).await;
        assert_eq!(read(&mut ws).await["type"], "PONG");
    }
    send(&mut ws, json!({"type":"PING"})).await;
    assert_eq!(read(&mut ws).await["code"], "RATE_LIMIT");
    bridge.shutdown().await;
}
#[tokio::test]
async fn unauthenticated_session_expires() {
    let bridge = Bridge::start().await.unwrap();
    let mut ws = socket(&bridge).await;
    assert!(timeout(Duration::from_secs(5), ws.next()).await.is_ok());
    bridge.shutdown().await;
}
#[tokio::test]
async fn unauthenticated_slots_are_bounded() {
    let bridge = Bridge::start().await.unwrap();
    let mut sockets = Vec::new();
    for _ in 0..4 {
        sockets.push(socket(&bridge).await);
    }
    let mut request = format!("{}/bridge", bridge.bootstrap.origin.replace("http:", "ws:"))
        .into_client_request()
        .unwrap();
    request
        .headers_mut()
        .insert("Origin", bridge.bootstrap.origin.parse().unwrap());
    assert!(connect_async(request)
        .await
        .unwrap_err()
        .to_string()
        .contains("429"));
    bridge.shutdown().await;
}
#[cfg(feature = "dev-echo")]
#[tokio::test]
async fn binary_roundtrip_only_to_bundled_echo_target() {
    let bridge = Bridge::start().await.unwrap();
    let mut ws = auth(&bridge).await;
    send(
        &mut ws,
        json!({"type":"CONNECT","host":"127.0.0.1","port":bridge.bootstrap.test_port}),
    )
    .await;
    assert_eq!(read(&mut ws).await["type"], "CONNECTED");
    let bytes = vec![0, 255, 128, 1, 0, 17];
    ws.send(Message::Binary(bytes.clone().into()))
        .await
        .unwrap();
    let response = timeout(Duration::from_secs(3), ws.next())
        .await
        .unwrap()
        .unwrap()
        .unwrap();
    assert_eq!(response.into_data().as_ref(), bytes.as_slice());
    send(&mut ws, json!({"type":"DISCONNECT"})).await;
    assert_eq!(read(&mut ws).await["type"], "DISCONNECTED");
    let port = bridge.bootstrap.test_port;
    bridge.shutdown().await;
    assert!(TcpStream::connect(("127.0.0.1", port)).await.is_err());
}

#[tokio::test]
async fn bounded_preferences_survive_session_replacement() {
    use lapis_bridge::preferences::Preferences;
    let bridge = Bridge::start().await.unwrap();
    let value = json!({"quality":"low","fps":30,"mouse":1.0,"touch":1.0,"scale":1.0,"distance":2,"host":"example.org","port":25565,"username":"Explorer"});
    let settings: Preferences = serde_json::from_value(value.clone()).unwrap();
    assert!(bridge.set_preferences(settings));
    let mut ws = auth(&bridge).await;
    assert_eq!(read(&mut ws).await["value"], value);
    let mut changed = value.clone();
    changed["fps"] = json!(60);
    send(&mut ws, json!({"type":"SETTINGS","value":changed})).await;
    assert_eq!(read(&mut ws).await["type"], "SETTINGS_OK");
    assert_eq!(bridge.preferences().unwrap().fps, 60);
    for (field, bad) in [
        ("fps", json!(999)),
        ("quality", json!("script")),
        ("scale", json!(-1)),
        ("username", json!("<script>")),
        ("host", json!("a/b")),
    ] {
        let mut input = value.clone();
        input[field] = bad;
        let invalid: Preferences = serde_json::from_value(input).unwrap();
        assert!(!bridge.set_preferences(invalid));
    }
    let mut extra = value.clone();
    extra["path"] = json!("/arbitrary/file");
    assert!(serde_json::from_value::<Preferences>(extra).is_err());
    bridge.shutdown().await;
}
