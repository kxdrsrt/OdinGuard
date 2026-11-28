//! EdDSA (BLAKE2b, Monocypher) and BLAKE2b, shared with the client DLL and the server module. Thin FFI wrapper.
#![allow(unsafe_code)]

unsafe extern "C" {
    fn crypto_eddsa_key_pair(secret_key: *mut u8, public_key: *mut u8, seed: *mut u8);
    fn crypto_eddsa_sign(signature: *mut u8, secret_key: *const u8, message: *const u8, message_size: usize);
    fn crypto_eddsa_check(signature: *const u8, public_key: *const u8, message: *const u8, message_size: usize) -> i32;
    fn crypto_blake2b(hash: *mut u8, hash_size: usize, message: *const u8, message_size: usize);
    fn crypto_blake2b_keyed(hash: *mut u8, hash_size: usize, key: *const u8, key_size: usize, message: *const u8, message_size: usize);
}

/// Public key of a 32-byte seed (the format of keys/server.seed and the Studio signing key).
pub fn public_key(seed: &[u8; 32]) -> [u8; 32] {
    let mut s = *seed;
    let mut sk = [0u8; 64];
    let mut pk = [0u8; 32];
    // SAFETY: buffers have the sizes Monocypher expects (64, 32, 32); the seed copy is wiped by Monocypher.
    unsafe { crypto_eddsa_key_pair(sk.as_mut_ptr(), pk.as_mut_ptr(), s.as_mut_ptr()) };
    sk.fill(0);
    pk
}

pub fn sign(seed: &[u8; 32], message: &[u8]) -> [u8; 64] {
    let mut s = *seed;
    let mut sk = [0u8; 64];
    let mut pk = [0u8; 32];
    let mut sig = [0u8; 64];
    // SAFETY: fixed-size buffers as required, message pointer/length from a slice.
    unsafe {
        crypto_eddsa_key_pair(sk.as_mut_ptr(), pk.as_mut_ptr(), s.as_mut_ptr());
        crypto_eddsa_sign(sig.as_mut_ptr(), sk.as_ptr(), message.as_ptr(), message.len());
    }
    sk.fill(0);
    sig
}

pub fn verify(public_key: &[u8; 32], message: &[u8], signature: &[u8; 64]) -> bool {
    // SAFETY: fixed-size buffers as required, message pointer/length from a slice.
    unsafe { crypto_eddsa_check(signature.as_ptr(), public_key.as_ptr(), message.as_ptr(), message.len()) == 0 }
}

pub fn blake2b<const N: usize>(message: &[u8]) -> [u8; N] {
    let mut out = [0u8; N];
    // SAFETY: N is the output size (1..=64), message from a slice.
    unsafe { crypto_blake2b(out.as_mut_ptr(), N, message.as_ptr(), message.len()) };
    out
}

pub fn blake2b_keyed<const N: usize>(key: &[u8], message: &[u8]) -> [u8; N] {
    let mut out = [0u8; N];
    // SAFETY: N is the output size (1..=64), key and message from slices (key at most 64 bytes, checked by callers).
    unsafe { crypto_blake2b_keyed(out.as_mut_ptr(), N, key.as_ptr(), key.len(), message.as_ptr(), message.len()) };
    out
}

/// Random bytes from the operating system (getrandom: /dev/urandom or getrandom(2), BCryptGenRandom on Windows).
pub fn random<const N: usize>() -> std::io::Result<[u8; N]> {
    let mut out = [0u8; N];
    getrandom::getrandom(&mut out).map_err(|e| std::io::Error::other(e.to_string()))?;
    Ok(out)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn sign_verify_and_tamper() {
        let seed = [7u8; 32];
        let pk = public_key(&seed);
        let sig = sign(&seed, b"odinguard");
        assert!(verify(&pk, b"odinguard", &sig));
        assert!(!verify(&pk, b"odinguarD", &sig));
    }
}
