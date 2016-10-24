extern crate afl;
extern crate ring;
extern crate openssl;

fn main() {
    afl::handle_bytes(|bytes| {
        let ring_digest = ring::digest::digest(&ring::digest::SHA256, &bytes);
        let openssl_digest = openssl::hash::hash(openssl::hash::MessageDigest::sha256(), &bytes).unwrap();
        assert_eq!(ring_digest.as_ref(), &openssl_digest[..]);
    });
}
