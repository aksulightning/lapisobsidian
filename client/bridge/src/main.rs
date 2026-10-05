// SPDX-License-Identifier: GPL-3.0-only
//! Headless verification harness, not the end-user launcher.
use std::io::{self, Read, Write};
#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    if std::env::args().skip(1).collect::<Vec<_>>() != ["--bootstrap-stdio"] {
        return Err("Use the standalone application. The test harness requires --bootstrap-stdio and private pipes.".into());
    }
    use std::io::IsTerminal;
    if io::stdout().is_terminal() {
        return Err("Refusing to print credentials to a terminal".into());
    }
    let bridge = lapis_bridge::Bridge::start().await?;
    // Private parent/child bootstrap pipe only; never a diagnostic log.
    println!("{}", serde_json::to_string(&bridge.bootstrap)?);
    io::stdout().flush()?;
    // A detached OS thread avoids blocking-stdin keeping runtime shutdown alive
    // when Ctrl-C arrives while the parent still holds its pipe open.
    let (closed, receive) = tokio::sync::oneshot::channel();
    std::thread::spawn(move || {
        let _ = io::stdin().read(&mut [0u8; 1]);
        let _ = closed.send(());
    });
    tokio::select! { _ = tokio::signal::ctrl_c() => {}, _ = receive => {} }
    bridge.shutdown().await;
    Ok(())
}
