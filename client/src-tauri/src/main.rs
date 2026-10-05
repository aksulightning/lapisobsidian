// SPDX-License-Identifier: GPL-3.0-only
#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]
use std::sync::Mutex;
use tauri::Manager;

fn main() {
    // Keep a runtime alive for the embedded bridge throughout the window lifetime.
    let runtime =
        tokio::runtime::Runtime::new().expect("Cannot start Lapis Obsidian Client runtime");
    let bridge = runtime
        .block_on(lapis_bridge::Bridge::start())
        .expect("Cannot start Lapis Obsidian Client local bridge");
    let bootstrap = bridge.bootstrap.clone();
    let origin = bootstrap.origin.clone();
    let init = format!("if (location.origin === {} && window === window.top) {{ Object.defineProperty(window, '__LAPIS_BOOTSTRAP__', {{value: {}, configurable: true}}); }}", serde_json::to_string(&origin).unwrap(), serde_json::to_string(&bootstrap).unwrap());
    let smoke_test = std::env::args().any(|arg| arg == "--smoke-test");
    let app = tauri::Builder::default()
        .manage(Mutex::new(Some(bridge)))
        .setup(move |app| {
            // Only this fixed, application-owned file is read. No frontend paths.
            if let Ok(directory) = app.path().app_config_dir() {
                let path = directory.join("preferences.json");
                if let Ok(file) = std::fs::File::open(path) {
                    use std::io::Read;
                    let mut bytes = Vec::new();
                    if file.take(4097).read_to_end(&mut bytes).is_ok() && bytes.len() <= 4096 {
                        if let Ok(value) = serde_json::from_slice(&bytes) {
                            if let Some(bridge) = app.state::<Mutex<Option<lapis_bridge::Bridge>>>().lock().unwrap().as_ref() { bridge.set_preferences(value); }
                        }
                    }
                }
            }
            let allowed = origin.clone();
            tauri::WebviewWindowBuilder::new(app, "main", tauri::WebviewUrl::External(origin.parse()?))
                .title("Lapis Obsidian Client")
                .inner_size(1200.0, 800.0).min_inner_size(360.0, 320.0)
                .initialization_script(&init)
                .on_navigation(move |url| url.origin().ascii_serialization() == allowed && url.path() == "/")
                .on_new_window(|_, _| tauri::webview::NewWindowResponse::Deny)
                .build()?;
            if smoke_test {
                let handle = app.handle().clone();
                tauri::async_runtime::spawn(async move {
                    let deadline = tokio::time::Instant::now() + std::time::Duration::from_secs(15);
                    loop {
                        let ready = handle.state::<Mutex<Option<lapis_bridge::Bridge>>>().lock().unwrap().as_ref().is_some_and(|b| b.is_authenticated());
                        if ready {
                            println!("Lapis Obsidian Client startup verified: local WebView authenticated.");
                            handle.exit(0);
                            break;
                        }
                        if tokio::time::Instant::now() >= deadline {
                            eprintln!("Lapis Obsidian Client startup check failed: WebView did not authenticate.");
                            handle.exit(2);
                            break;
                        }
                        tokio::time::sleep(std::time::Duration::from_millis(50)).await;
                    }
                });
            }
            Ok(())
        })
        .build(tauri::generate_context!())
        .expect("Cannot create Lapis Obsidian Client window");
    app.run(move |handle, event| {
        if matches!(event, tauri::RunEvent::Exit) {
            if let Some(bridge) = handle
                .state::<Mutex<Option<lapis_bridge::Bridge>>>()
                .lock()
                .unwrap()
                .take()
            {
                if let Some(value) = bridge.preferences() {
                    let saved = (|| -> Result<(), Box<dyn std::error::Error>> {
                        use std::io::Write;
                        let directory = handle.path().app_config_dir()?;
                        std::fs::create_dir_all(&directory)?;
                        // Per-instance temporary file, then an atomic rename on Unix.
                        let temporary =
                            directory.join(format!("preferences-{}.tmp", std::process::id()));
                        let mut options = std::fs::OpenOptions::new();
                        options.write(true).create_new(true);
                        #[cfg(unix)]
                        {
                            use std::os::unix::fs::OpenOptionsExt;
                            options.mode(0o600);
                        }
                        let mut file = options.open(&temporary)?;
                        file.write_all(&serde_json::to_vec(&value)?)?;
                        file.sync_all()?;
                        drop(file);
                        let destination = directory.join("preferences.json");
                        #[cfg(windows)]
                        if destination.exists() {
                            std::fs::remove_file(&destination)?;
                        }
                        std::fs::rename(temporary, destination)?;
                        Ok(())
                    })();
                    if saved.is_err() {
                        eprintln!("Lapis Obsidian Client could not save preferences.");
                    }
                }
                runtime.block_on(bridge.shutdown());
            }
        }
    });
}
