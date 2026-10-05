fn main() {
    // include_dir expands at compile time; ensure asset-only edits invalidate it.
    println!("cargo:rerun-if-changed=../dist");
}
