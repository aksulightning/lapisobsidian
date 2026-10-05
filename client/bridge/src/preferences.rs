// SPDX-License-Identifier: GPL-3.0-only
use serde::{Deserialize, Serialize};
/// The only persistable data. No paths, credentials, URLs or executable content.
#[derive(Clone, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Preferences {
    pub quality: String,
    pub fps: u16,
    pub mouse: f64,
    pub touch: f64,
    pub scale: f64,
    pub distance: u8,
    pub host: String,
    pub port: u16,
    pub username: String,
}
impl Preferences {
    pub fn valid(&self) -> bool {
        matches!(self.quality.as_str(), "low" | "medium" | "high")
            && matches!(self.fps, 30 | 60 | 120)
            && self.mouse.is_finite()
            && (0.2..=3.0).contains(&self.mouse)
            && self.touch.is_finite()
            && (0.2..=3.0).contains(&self.touch)
            && self.scale.is_finite()
            && (0.85..=1.2).contains(&self.scale)
            && matches!(self.distance, 2 | 4 | 8)
            && (self.host.is_empty() || super::valid_target(&self.host, self.port))
            && self.port > 0
            && !self.username.is_empty()
            && self.username.len() <= 15
            && self
                .username
                .bytes()
                .all(|b| b.is_ascii_alphanumeric() || b == b'_')
    }
}
