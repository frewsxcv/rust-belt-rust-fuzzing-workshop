use libc::{c_char, c_int, c_long, c_ulong, c_uint, c_void};
use std::io;
use std::io::prelude::*;
use std::cmp::Ordering;
use std::ffi::CString;
use std::iter::repeat;
use std::mem;
use std::ptr;
use std::ops::Deref;
use std::fmt;
use std::str;
use std::slice;
use std::collections::HashMap;
use std::marker::PhantomData;

use asn1::Asn1Time;
use bio::MemBio;
use crypto::hash;
use crypto::hash::Type as HashType;
use crypto::pkey::{PKey, Parts};
use crypto::rand::rand_bytes;
use ffi;
use ffi_extras;
use ssl::error::{SslError, StreamError};
use nid::Nid;

pub mod extension;

use self::extension::{ExtensionType, Extension};

#[cfg(test)]
mod tests;

pub struct SslString(&'static str);

impl<'s> Drop for SslString {
    fn drop(&mut self) {
        unsafe {
            ffi::CRYPTO_free(self.0.as_ptr() as *mut c_void);
        }
    }
}

impl Deref for SslString {
    type Target = str;

    fn deref(&self) -> &str {
        self.0
    }
}

impl SslString {
    unsafe fn new(buf: *const c_char, len: c_int) -> SslString {
        let slice = slice::from_raw_parts(buf as *const _, len as usize);
        SslString(str::from_utf8_unchecked(slice))
    }
}

impl fmt::Display for SslString {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        fmt::Display::fmt(self.0, f)
    }
}

impl fmt::Debug for SslString {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        fmt::Debug::fmt(self.0, f)
    }
}

#[derive(Copy, Clone)]
#[repr(i32)]
pub enum X509FileType {
    PEM = ffi::X509_FILETYPE_PEM,
    ASN1 = ffi::X509_FILETYPE_ASN1,
    Default = ffi::X509_FILETYPE_DEFAULT,
}

#[allow(missing_copy_implementations)]
pub struct X509StoreContext {
    ctx: *mut ffi::X509_STORE_CTX,
}

impl X509StoreContext {
    pub fn new(ctx: *mut ffi::X509_STORE_CTX) -> X509StoreContext {
        X509StoreContext { ctx: ctx }
    }

    pub fn get_error(&self) -> Option<X509ValidationError> {
        let err = unsafe { ffi::X509_STORE_CTX_get_error(self.ctx) };
        X509ValidationError::from_raw(err)
    }

    pub fn get_current_cert<'a>(&'a self) -> Option<X509<'a>> {
        let ptr = unsafe { ffi::X509_STORE_CTX_get_current_cert(self.ctx) };

        if ptr.is_null() {
            None
        } else {
            Some(X509 {
                ctx: Some(self),
                handle: ptr,
                owned: false,
            })
        }
    }

    pub fn error_depth(&self) -> u32 {
        unsafe { ffi::X509_STORE_CTX_get_error_depth(self.ctx) as u32 }
    }
}

#[allow(non_snake_case)]
/// Generator of private key/certificate pairs
///
/// # Example
///
/// ```
/// # #[allow(unstable)]
/// # fn main() {
/// use std::fs;
/// use std::fs::File;
/// use std::io::prelude::*;
/// use std::path::Path;
///
/// use openssl::crypto::hash::Type;
/// use openssl::x509::X509Generator;
/// use openssl::x509::extension::{Extension, KeyUsageOption};
///
/// let gen = X509Generator::new()
///        .set_bitlength(2048)
///        .set_valid_period(365*2)
///        .add_name("CN".to_owned(), "SuperMegaCorp Inc.".to_owned())
///        .set_sign_hash(Type::SHA256)
///        .add_extension(Extension::KeyUsage(vec![KeyUsageOption::DigitalSignature]));
///
/// let (cert, pkey) = gen.generate().unwrap();
///
/// let cert_path = "doc_cert.pem";
/// let mut file = File::create(cert_path).unwrap();
/// assert!(cert.write_pem(&mut file).is_ok());
/// # let _ = fs::remove_file(cert_path);
///
/// let pkey_path = "doc_key.pem";
/// let mut file = File::create(pkey_path).unwrap();
/// assert!(pkey.write_pem(&mut file).is_ok());
/// # let _ = fs::remove_file(pkey_path);
/// # }
/// ```
pub struct X509Generator {
    bits: u32,
    days: u32,
    names: Vec<(String, String)>,
    extensions: Extensions,
    hash_type: HashType,
}

impl X509Generator {
    /// Creates a new generator with the following defaults:
    ///
    /// bit length: 1024
    ///
    /// validity period: 365 days
    ///
    /// CN: "rust-openssl"
    ///
    /// hash: SHA1
    pub fn new() -> X509Generator {
        X509Generator {
            bits: 1024,
            days: 365,
            names: vec![],
            extensions: Extensions::new(),
            hash_type: HashType::SHA1,
        }
    }

    /// Sets desired bit length
    pub fn set_bitlength(mut self, bits: u32) -> X509Generator {
        self.bits = bits;
        self
    }

    /// Sets certificate validity period in days since today
    pub fn set_valid_period(mut self, days: u32) -> X509Generator {
        self.days = days;
        self
    }

    /// Add attribute to the name of the certificate
    ///
    /// ```
    /// # let generator = openssl::x509::X509Generator::new();
    /// generator.add_name("CN".to_string(),"example.com".to_string());
    /// ```
    pub fn add_name(mut self, attr_type: String, attr_value: String) -> X509Generator {
        self.names.push((attr_type, attr_value));
        self
    }

    /// Add multiple attributes to the name of the certificate
    ///
    /// ```
    /// # let generator = openssl::x509::X509Generator::new();
    /// generator.add_names(vec![("CN".to_string(),"example.com".to_string())]);
    /// ```
    pub fn add_names<I>(mut self, attrs: I) -> X509Generator
        where I: IntoIterator<Item = (String, String)>
    {
        self.names.extend(attrs);
        self
    }

    /// Add an extension to a certificate
    ///
    /// If the extension already exists, it will be replaced.
    ///
    /// ```
    /// use openssl::x509::extension::Extension::*;
    /// use openssl::x509::extension::KeyUsageOption::*;
    ///
    /// # let generator = openssl::x509::X509Generator::new();
    /// generator.add_extension(KeyUsage(vec![DigitalSignature, KeyEncipherment]));
    /// ```
    pub fn add_extension(mut self, ext: extension::Extension) -> X509Generator {
        self.extensions.add(ext);
        self
    }

    /// Add multiple extensions to a certificate
    ///
    /// If any of the extensions already exist, they will be replaced.
    ///
    /// ```
    /// use openssl::x509::extension::Extension::*;
    /// use openssl::x509::extension::KeyUsageOption::*;
    ///
    /// # let generator = openssl::x509::X509Generator::new();
    /// generator.add_extensions(vec![KeyUsage(vec![DigitalSignature, KeyEncipherment])]);
    /// ```
    pub fn add_extensions<I>(mut self, exts: I) -> X509Generator
        where I: IntoIterator<Item = extension::Extension>
    {
        for ext in exts {
            self.extensions.add(ext);
        }

        self
    }

    pub fn set_sign_hash(mut self, hash_type: hash::Type) -> X509Generator {
        self.hash_type = hash_type;
        self
    }

    fn add_extension_internal(x509: *mut ffi::X509,
                              exttype: &extension::ExtensionType,
                              value: &str)
                              -> Result<(), SslError> {
        unsafe {
            let mut ctx: ffi::X509V3_CTX = mem::zeroed();
            ffi::X509V3_set_ctx(&mut ctx, x509, x509, ptr::null_mut(), ptr::null_mut(), 0);
            let value = CString::new(value.as_bytes()).unwrap();
            let ext = match exttype.get_nid() {
                Some(nid) => {
                    ffi::X509V3_EXT_conf_nid(ptr::null_mut(),
                                             mem::transmute(&ctx),
                                             nid as c_int,
                                             value.as_ptr() as *mut c_char)
                }
                None => {
                    let name = CString::new(exttype.get_name().unwrap().as_bytes()).unwrap();
                    ffi::X509V3_EXT_conf(ptr::null_mut(),
                                         mem::transmute(&ctx),
                                         name.as_ptr() as *mut c_char,
                                         value.as_ptr() as *mut c_char)
                }
            };
            let mut success = false;
            if ext != ptr::null_mut() {
                success = ffi::X509_add_ext(x509, ext, -1) != 0;
                ffi::X509_EXTENSION_free(ext);
            }
            lift_ssl_if!(!success)
        }
    }

    fn add_name_internal(name: *mut ffi::X509_NAME,
                         key: &str,
                         value: &str)
                         -> Result<(), SslError> {
        let value_len = value.len() as c_int;
        lift_ssl!(unsafe {
            let key = CString::new(key.as_bytes()).unwrap();
            let value = CString::new(value.as_bytes()).unwrap();
            ffi::X509_NAME_add_entry_by_txt(name,
                                            key.as_ptr() as *const _,
                                            ffi::MBSTRING_UTF8,
                                            value.as_ptr() as *const _,
                                            value_len,
                                            -1,
                                            0)
        })
    }

    fn random_serial() -> c_long {
        let len = mem::size_of::<c_long>();
        let bytes = rand_bytes(len);
        let mut res = 0;
        for b in bytes.iter() {
            res = res << 8;
            res |= (*b as c_long) & 0xff;
        }

        // While OpenSSL is actually OK to have negative serials
        // other libraries (for example, Go crypto) can drop
        // such certificates as invalid, so we clear the high bit
        ((res as c_ulong) >> 1) as c_long
    }

    /// Generates a private key and a self-signed certificate and returns them
    pub fn generate<'a>(&self) -> Result<(X509<'a>, PKey), SslError> {
        ffi::init();

        let mut p_key = PKey::new();
        p_key.gen(self.bits as usize);

        let x509 = try!(self.sign(&p_key));
        Ok((x509, p_key))
    }

    /// Sets the certificate public-key, then self-sign and return it
    /// Note: That the bit-length of the private key is used (set_bitlength is ignored)
    pub fn sign<'a>(&self, p_key: &PKey) -> Result<X509<'a>, SslError> {
        ffi::init();

        unsafe {
            let x509 = ffi::X509_new();
            try_ssl_null!(x509);

            let x509 = X509 {
                handle: x509,
                ctx: None,
                owned: true,
            };

            try_ssl!(ffi::X509_set_version(x509.handle, 2));
            try_ssl!(ffi::ASN1_INTEGER_set(ffi::X509_get_serialNumber(x509.handle),
                                           X509Generator::random_serial()));

            let not_before = try!(Asn1Time::days_from_now(0));
            let not_after = try!(Asn1Time::days_from_now(self.days));

            try_ssl!(ffi::X509_set_notBefore(x509.handle, mem::transmute(not_before.get_handle())));
            // If prev line succeded - ownership should go to cert
            mem::forget(not_before);

            try_ssl!(ffi::X509_set_notAfter(x509.handle, mem::transmute(not_after.get_handle())));
            // If prev line succeded - ownership should go to cert
            mem::forget(not_after);

            try_ssl!(ffi::X509_set_pubkey(x509.handle, p_key.get_handle()));

            let name = ffi::X509_get_subject_name(x509.handle);
            try_ssl_null!(name);

            let default = [("CN", "rust-openssl")];
            let default_iter = &mut default.iter().map(|&(k, v)| (k, v));
            let arg_iter = &mut self.names.iter().map(|&(ref k, ref v)| (&k[..], &v[..]));
            let iter: &mut Iterator<Item = (&str, &str)> = if self.names.len() == 0 {
                default_iter
            } else {
                arg_iter
            };

            for (key, val) in iter {
                try!(X509Generator::add_name_internal(name, &key, &val));
            }
            ffi::X509_set_issuer_name(x509.handle, name);

            for (exttype, ext) in self.extensions.iter() {
                try!(X509Generator::add_extension_internal(x509.handle,
                                                           &exttype,
                                                           &ext.to_string()));
            }

            let hash_fn = self.hash_type.evp_md();
            try_ssl!(ffi::X509_sign(x509.handle, p_key.get_handle(), hash_fn));
            Ok(x509)
        }
    }

    /// Obtain a certificate signing request (CSR)
    pub fn request(&self, p_key: &PKey) -> Result<X509Req, SslError> {
        let cert = match self.sign(p_key) {
            Ok(c) => c,
            Err(x) => return Err(x),
        };

        unsafe {
            let req = ffi::X509_to_X509_REQ(cert.handle, ptr::null_mut(), ptr::null());
            try_ssl_null!(req);

            let exts = ffi_extras::X509_get_extensions(cert.handle);
            if exts != ptr::null_mut() {
                try_ssl!(ffi::X509_REQ_add_extensions(req, exts));
            }

            let hash_fn = self.hash_type.evp_md();
            try_ssl!(ffi::X509_REQ_sign(req, p_key.get_handle(), hash_fn));

            Ok(X509Req::new(req))
        }
    }
}


#[allow(dead_code)]
/// A public key certificate
pub struct X509<'ctx> {
    ctx: Option<&'ctx X509StoreContext>,
    handle: *mut ffi::X509,
    owned: bool,
}

impl<'ctx> X509<'ctx> {
    /// Creates new from handle with desired ownership.
    pub fn new(handle: *mut ffi::X509, owned: bool) -> X509<'ctx> {
        X509 {
            ctx: None,
            handle: handle,
            owned: owned,
        }
    }

    /// Creates a new certificate from context. Doesn't take ownership
    /// of handle.
    pub fn new_in_ctx(handle: *mut ffi::X509, ctx: &'ctx X509StoreContext) -> X509<'ctx> {
        X509 {
            ctx: Some(ctx),
            handle: handle,
            owned: false,
        }
    }

    /// Reads certificate from PEM, takes ownership of handle
    pub fn from_pem<R>(reader: &mut R) -> Result<X509<'ctx>, SslError>
        where R: Read
    {
        let mut mem_bio = try!(MemBio::new());
        try!(io::copy(reader, &mut mem_bio).map_err(StreamError));

        unsafe {
            let handle = try_ssl_null!(ffi::PEM_read_bio_X509(mem_bio.get_handle(),
                                                              ptr::null_mut(),
                                                              None,
                                                              ptr::null_mut()));
            Ok(X509::new(handle, true))
        }
    }

    pub fn get_handle(&self) -> *mut ffi::X509 {
        self.handle
    }

    pub fn subject_name<'a>(&'a self) -> X509Name<'a> {
        let name = unsafe { ffi::X509_get_subject_name(self.handle) };
        X509Name {
            x509: self,
            name: name,
        }
    }

    /// Returns this certificate's SAN entries, if they exist.
    pub fn subject_alt_names<'a>(&'a self) -> Option<GeneralNames<'a>> {
        unsafe {
            let stack = ffi::X509_get_ext_d2i(self.handle,
                                              Nid::SubjectAltName as c_int,
                                              ptr::null_mut(),
                                              ptr::null_mut());
            if stack.is_null() {
                return None;
            }

            Some(GeneralNames {
                stack: stack as *const _,
                m: PhantomData,
            })
        }
    }

    pub fn public_key(&self) -> PKey {
        let pkey = unsafe { ffi::X509_get_pubkey(self.handle) };
        assert!(!pkey.is_null());

        PKey::from_handle(pkey, Parts::Public)
    }

    /// Returns certificate fingerprint calculated using provided hash
    pub fn fingerprint(&self, hash_type: hash::Type) -> Option<Vec<u8>> {
        let evp = hash_type.evp_md();
        let len = hash_type.md_len();
        let v: Vec<u8> = repeat(0).take(len as usize).collect();
        let act_len: c_uint = 0;
        let res = unsafe {
            ffi::X509_digest(self.handle,
                             evp,
                             mem::transmute(v.as_ptr()),
                             mem::transmute(&act_len))
        };

        match res {
            0 => None,
            _ => {
                let act_len = act_len as usize;
                match len.cmp(&act_len) {
                    Ordering::Greater => None,
                    Ordering::Equal => Some(v),
                    Ordering::Less => panic!("Fingerprint buffer was corrupted!"),
                }
            }
        }
    }

    /// Writes certificate as PEM
    pub fn write_pem<W>(&self, writer: &mut W) -> Result<(), SslError>
        where W: Write
    {
        let mut mem_bio = try!(MemBio::new());
        unsafe {
            try_ssl!(ffi::PEM_write_bio_X509(mem_bio.get_handle(), self.handle));
        }
        io::copy(&mut mem_bio, writer).map_err(StreamError).map(|_| ())
    }
}

extern "C" {
    fn rust_X509_clone(x509: *mut ffi::X509);
}

impl<'ctx> Clone for X509<'ctx> {
    fn clone(&self) -> X509<'ctx> {
        unsafe { rust_X509_clone(self.handle) }
        // FIXME: given that we now have refcounting control, 'owned' should be uneeded, the 'ctx
        // is probably also uneeded. We can remove both to condense the x509 api quite a bit
        //
        X509::new(self.handle, true)
    }
}

impl<'ctx> Drop for X509<'ctx> {
    fn drop(&mut self) {
        if self.owned {
            unsafe { ffi::X509_free(self.handle) };
        }
    }
}

#[allow(dead_code)]
pub struct X509Name<'x> {
    x509: &'x X509<'x>,
    name: *mut ffi::X509_NAME,
}

#[allow(dead_code)]
pub struct X509NameEntry<'x> {
    x509_name: &'x X509Name<'x>,
    ne: *mut ffi::X509_NAME_ENTRY,
}

impl<'x> X509Name<'x> {
    pub fn text_by_nid(&self, nid: Nid) -> Option<SslString> {
        unsafe {
            let loc = ffi::X509_NAME_get_index_by_NID(self.name, nid as c_int, -1);
            if loc == -1 {
                return None;
            }

            let ne = ffi::X509_NAME_get_entry(self.name, loc);
            if ne.is_null() {
                return None;
            }

            let asn1_str = ffi::X509_NAME_ENTRY_get_data(ne);
            if asn1_str.is_null() {
                return None;
            }

            let mut str_from_asn1: *mut c_char = ptr::null_mut();
            let len = ffi::ASN1_STRING_to_UTF8(&mut str_from_asn1, asn1_str);

            if len < 0 {
                return None;
            }

            assert!(!str_from_asn1.is_null());

            Some(SslString::new(str_from_asn1, len))
        }
    }
}

/// A certificate signing request
pub struct X509Req {
    handle: *mut ffi::X509_REQ,
}

impl X509Req {
    /// Creates new from handle
    pub fn new(handle: *mut ffi::X509_REQ) -> X509Req {
        X509Req { handle: handle }
    }

    /// Reads CSR from PEM
    pub fn from_pem<R>(reader: &mut R) -> Result<X509Req, SslError>
        where R: Read
    {
        let mut mem_bio = try!(MemBio::new());
        try!(io::copy(reader, &mut mem_bio).map_err(StreamError));

        unsafe {
            let handle = try_ssl_null!(ffi::PEM_read_bio_X509_REQ(mem_bio.get_handle(),
                                                                  ptr::null_mut(),
                                                                  None,
                                                                  ptr::null_mut()));
            Ok(X509Req::new(handle))
        }
    }

    /// Writes CSR as PEM
    pub fn write_pem<W>(&self, writer: &mut W) -> Result<(), SslError>
        where W: Write
    {
        let mut mem_bio = try!(MemBio::new());
        unsafe {
            try_ssl!(ffi::PEM_write_bio_X509_REQ(mem_bio.get_handle(), self.handle));
        }
        io::copy(&mut mem_bio, writer).map_err(StreamError).map(|_| ())
    }
}

impl Drop for X509Req {
    fn drop(&mut self) {
        unsafe { ffi::X509_REQ_free(self.handle) };
    }
}

/// A collection of X.509 extensions.
///
/// Upholds the invariant that a certificate MUST NOT include more than one
/// instance of a particular extension, according to RFC 3280 §4.2. Also
/// ensures that extensions are added to the certificate during signing
/// in the order they were inserted, which is required for certain
/// extensions like SubjectKeyIdentifier and AuthorityKeyIdentifier.
struct Extensions {
    /// The extensions contained in the collection.
    extensions: Vec<Extension>,
    /// A map of used to keep track of added extensions and their indexes in `self.extensions`.
    indexes: HashMap<ExtensionType, usize>,
}

impl Extensions {
    /// Creates a new `Extensions`.
    pub fn new() -> Extensions {
        Extensions {
            extensions: vec![],
            indexes: HashMap::new(),
        }
    }

    /// Adds a new `Extension`, replacing any existing one of the same
    /// `ExtensionType`.
    pub fn add(&mut self, ext: Extension) {
        let ext_type = ext.get_type();

        if let Some(index) = self.indexes.get(&ext_type) {
            self.extensions[*index] = ext;
            return;
        }

        self.extensions.push(ext);
        self.indexes.insert(ext_type, self.extensions.len() - 1);
    }

    /// Returns an `ExtensionsIter` for the collection.
    pub fn iter(&self) -> ExtensionsIter {
        ExtensionsIter {
            current: 0,
            extensions: &self.extensions,
        }
    }
}

/// An iterator that iterates over `(ExtensionType, Extension)` for each
/// extension in the collection.
struct ExtensionsIter<'a> {
    current: usize,
    extensions: &'a Vec<Extension>,
}

impl<'a> Iterator for ExtensionsIter<'a> {
    type Item = (ExtensionType, &'a Extension);

    fn next(&mut self) -> Option<Self::Item> {
        if self.current < self.extensions.len() {
            let ext = &self.extensions[self.current];

            self.current += 1;

            Some((ext.get_type(), ext))
        } else {
            None
        }
    }
}

macro_rules! make_validation_error(
    ($ok_val:ident, $($name:ident = $val:ident,)+) => (
        #[derive(Copy, Clone)]
        pub enum X509ValidationError {
            $($name,)+
            X509UnknownError(c_int)
        }

        impl X509ValidationError {
            #[doc(hidden)]
            pub fn from_raw(err: c_int) -> Option<X509ValidationError> {
                match err {
                    ffi::$ok_val => None,
                    $(ffi::$val => Some(X509ValidationError::$name),)+
                    err => Some(X509ValidationError::X509UnknownError(err))
                }
            }
        }
    )
);

make_validation_error!(X509_V_OK,
    X509UnableToGetIssuerCert = X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT,
    X509UnableToGetCrl = X509_V_ERR_UNABLE_TO_GET_CRL,
    X509UnableToDecryptCertSignature = X509_V_ERR_UNABLE_TO_DECRYPT_CERT_SIGNATURE,
    X509UnableToDecryptCrlSignature = X509_V_ERR_UNABLE_TO_DECRYPT_CRL_SIGNATURE,
    X509UnableToDecodeIssuerPublicKey = X509_V_ERR_UNABLE_TO_DECODE_ISSUER_PUBLIC_KEY,
    X509CertSignatureFailure = X509_V_ERR_CERT_SIGNATURE_FAILURE,
    X509CrlSignatureFailure = X509_V_ERR_CRL_SIGNATURE_FAILURE,
    X509CertNotYetValid = X509_V_ERR_CERT_NOT_YET_VALID,
    X509CertHasExpired = X509_V_ERR_CERT_HAS_EXPIRED,
    X509CrlNotYetValid = X509_V_ERR_CRL_NOT_YET_VALID,
    X509CrlHasExpired = X509_V_ERR_CRL_HAS_EXPIRED,
    X509ErrorInCertNotBeforeField = X509_V_ERR_ERROR_IN_CERT_NOT_BEFORE_FIELD,
    X509ErrorInCertNotAfterField = X509_V_ERR_ERROR_IN_CERT_NOT_AFTER_FIELD,
    X509ErrorInCrlLastUpdateField = X509_V_ERR_ERROR_IN_CRL_LAST_UPDATE_FIELD,
    X509ErrorInCrlNextUpdateField = X509_V_ERR_ERROR_IN_CRL_NEXT_UPDATE_FIELD,
    X509OutOfMem = X509_V_ERR_OUT_OF_MEM,
    X509DepthZeroSelfSignedCert = X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT,
    X509SelfSignedCertInChain = X509_V_ERR_SELF_SIGNED_CERT_IN_CHAIN,
    X509UnableToGetIssuerCertLocally = X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT_LOCALLY,
    X509UnableToVerifyLeafSignature = X509_V_ERR_UNABLE_TO_VERIFY_LEAF_SIGNATURE,
    X509CertChainTooLong = X509_V_ERR_CERT_CHAIN_TOO_LONG,
    X509CertRevoked = X509_V_ERR_CERT_REVOKED,
    X509InvalidCA = X509_V_ERR_INVALID_CA,
    X509PathLengthExceeded = X509_V_ERR_PATH_LENGTH_EXCEEDED,
    X509InvalidPurpose = X509_V_ERR_INVALID_PURPOSE,
    X509CertUntrusted = X509_V_ERR_CERT_UNTRUSTED,
    X509CertRejected = X509_V_ERR_CERT_REJECTED,
    X509SubjectIssuerMismatch = X509_V_ERR_SUBJECT_ISSUER_MISMATCH,
    X509AkidSkidMismatch = X509_V_ERR_AKID_SKID_MISMATCH,
    X509AkidIssuerSerialMismatch = X509_V_ERR_AKID_ISSUER_SERIAL_MISMATCH,
    X509KeyusageNoCertsign = X509_V_ERR_KEYUSAGE_NO_CERTSIGN,
    X509UnableToGetCrlIssuer = X509_V_ERR_UNABLE_TO_GET_CRL_ISSUER,
    X509UnhandledCriticalExtension = X509_V_ERR_UNHANDLED_CRITICAL_EXTENSION,
    X509KeyusageNoCrlSign = X509_V_ERR_KEYUSAGE_NO_CRL_SIGN,
    X509UnhandledCriticalCrlExtension = X509_V_ERR_UNHANDLED_CRITICAL_CRL_EXTENSION,
    X509InvalidNonCA = X509_V_ERR_INVALID_NON_CA,
    X509ProxyPathLengthExceeded = X509_V_ERR_PROXY_PATH_LENGTH_EXCEEDED,
    X509KeyusageNoDigitalSignature = X509_V_ERR_KEYUSAGE_NO_DIGITAL_SIGNATURE,
    X509ProxyCertificatesNotAllowed = X509_V_ERR_PROXY_CERTIFICATES_NOT_ALLOWED,
    X509InvalidExtension = X509_V_ERR_INVALID_EXTENSION,
    X509InavlidPolicyExtension = X509_V_ERR_INVALID_POLICY_EXTENSION,
    X509NoExplicitPolicy = X509_V_ERR_NO_EXPLICIT_POLICY,
    X509DifferentCrlScope = X509_V_ERR_DIFFERENT_CRL_SCOPE,
    X509UnsupportedExtensionFeature = X509_V_ERR_UNSUPPORTED_EXTENSION_FEATURE,
    X509UnnestedResource = X509_V_ERR_UNNESTED_RESOURCE,
    X509PermittedVolation = X509_V_ERR_PERMITTED_VIOLATION,
    X509ExcludedViolation = X509_V_ERR_EXCLUDED_VIOLATION,
    X509SubtreeMinmax = X509_V_ERR_SUBTREE_MINMAX,
    X509UnsupportedConstraintType = X509_V_ERR_UNSUPPORTED_CONSTRAINT_TYPE,
    X509UnsupportedConstraintSyntax = X509_V_ERR_UNSUPPORTED_CONSTRAINT_SYNTAX,
    X509UnsupportedNameSyntax = X509_V_ERR_UNSUPPORTED_NAME_SYNTAX,
    X509CrlPathValidationError= X509_V_ERR_CRL_PATH_VALIDATION_ERROR,
    X509ApplicationVerification = X509_V_ERR_APPLICATION_VERIFICATION,
);

/// A collection of OpenSSL `GENERAL_NAME`s.
pub struct GeneralNames<'a> {
    stack: *const ffi::stack_st_GENERAL_NAME,
    m: PhantomData<&'a ()>,
}

impl<'a> GeneralNames<'a> {
    /// Returns the number of `GeneralName`s in this structure.
    pub fn len(&self) -> usize {
        unsafe { (*self.stack).stack.num as usize }
    }

    /// Returns the specified `GeneralName`.
    ///
    /// # Panics
    ///
    /// Panics if `idx` is not less than `len()`.
    pub fn get(&self, idx: usize) -> GeneralName<'a> {
        unsafe {
            assert!(idx < self.len());

            GeneralName {
                name: *(*self.stack).stack.data.offset(idx as isize) as *const ffi::GENERAL_NAME,
                m: PhantomData,
            }
        }
    }

    /// Returns an iterator over the `GeneralName`s in this structure.
    pub fn iter(&self) -> GeneralNamesIter {
        GeneralNamesIter {
            names: self,
            idx: 0,
        }
    }
}

impl<'a> IntoIterator for &'a GeneralNames<'a> {
    type Item = GeneralName<'a>;
    type IntoIter = GeneralNamesIter<'a>;

    fn into_iter(self) -> GeneralNamesIter<'a> {
        self.iter()
    }
}

/// An iterator over OpenSSL `GENERAL_NAME`s.
pub struct GeneralNamesIter<'a> {
    names: &'a GeneralNames<'a>,
    idx: usize,
}

impl<'a> Iterator for GeneralNamesIter<'a> {
    type Item = GeneralName<'a>;

    fn next(&mut self) -> Option<Self::Item> {
        if self.idx < self.names.len() {
            let name = self.names.get(self.idx);
            self.idx += 1;
            Some(name)
        } else {
            None
        }
    }

    fn size_hint(&self) -> (usize, Option<usize>) {
        let size = self.names.len() - self.idx;
        (size, Some(size))
    }
}

impl<'a> ExactSizeIterator for GeneralNamesIter<'a> {}

/// An OpenSSL `GENERAL_NAME`.
pub struct GeneralName<'a> {
    name: *const ffi::GENERAL_NAME,
    m: PhantomData<&'a ()>,
}

impl<'a> GeneralName<'a> {
    /// Returns the contents of this `GeneralName` if it is a `dNSName`.
    pub fn dnsname(&self) -> Option<&str> {
        unsafe {
            if (*self.name).type_ != ffi::GEN_DNS {
                return None;
            }

            let ptr = ffi::ASN1_STRING_data((*self.name).d as *mut _);
            let len = ffi::ASN1_STRING_length((*self.name).d as *mut _);

            let slice = slice::from_raw_parts(ptr as *const u8, len as usize);
            // dNSNames are stated to be ASCII (specifically IA5). Hopefully
            // OpenSSL checks that when loading a certificate but if not we'll
            // use this instead of from_utf8_unchecked just in case.
            str::from_utf8(slice).ok()
        }
    }

    /// Returns the contents of this `GeneralName` if it is an `iPAddress`.
    pub fn ipaddress(&self) -> Option<&[u8]> {
        unsafe {
            if (*self.name).type_ != ffi::GEN_IPADD {
                return None;
            }

            let ptr = ffi::ASN1_STRING_data((*self.name).d as *mut _);
            let len = ffi::ASN1_STRING_length((*self.name).d as *mut _);

            Some(slice::from_raw_parts(ptr as *const u8, len as usize))
        }
    }
}

#[test]
fn test_negative_serial() {
    // I guess that's enough to get a random negative number
    for _ in 0..1000 {
        assert!(X509Generator::random_serial() > 0,
                "All serials should be positive");
    }
}
