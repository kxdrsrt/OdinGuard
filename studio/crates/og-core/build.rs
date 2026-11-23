// Monocypher (third_party, CC0/BSD-2) is the same crypto library the client DLL and the server module use: the Studio
// signs with EdDSA (BLAKE2b) exactly as the server and the client verify.
fn main() {
    let src = "../../../third_party/monocypher.c";
    println!("cargo:rerun-if-changed={src}");
    cc::Build::new().file(src).opt_level(2).warnings(false).compile("monocypher");
}
