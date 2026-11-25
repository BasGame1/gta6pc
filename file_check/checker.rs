use serde::Deserialize;
use sha2::{Digest, Sha256};
use std::collections::HashMap;
use std::env;
use std::fs::{self, File};
use std::io::{self, Read, Write};
use std::path::Path;
use std::process;

#[derive(Deserialize)]
struct Manifest {
    title: Option<String>,
    version: Option<String>,
    files: HashMap<String, Info>,
}

#[derive(Deserialize)]
struct Info {
    size: u64,
    sha256: String,
}

fn usage() -> ! {
    eprintln!("USAGE:\nchecker[.exe] <dir with files to check> <path to the hash table json>");
    process::exit(1);
}

fn decode_hex_32(hex: &str) -> Option<[u8; 32]> {
    let bytes = hex.trim().as_bytes();
    if bytes.len() != 64 {
        return None;
    }
    let mut out = [0u8; 32];
    for (i, chunk) in bytes.chunks_exact(2).enumerate() {
        let hi = (chunk[0] as char).to_digit(16)? as u8;
        let lo = (chunk[1] as char).to_digit(16)? as u8;
        out[i] = (hi << 4) | lo;
    }
    Some(out)
}

fn verify_hash(path: &Path, expected: &[u8; 32], hasher: &mut Sha256, buf: &mut [u8]) -> io::Result<bool> {
    let mut file = File::open(path)?;
    loop {
        let n = file.read(buf)?;
        if n == 0 {
            break;
        }
        hasher.update(&buf[..n]);
    }
    Ok(hasher.finalize_reset().as_slice() == expected)
}

fn main() {
    let mut args = env::args().skip(1);
    let (Some(dir_arg), Some(json_arg), None) = (args.next(), args.next(), args.next()) else {
        eprintln!("Error: Arguments should only be a folder to check and hash table");
        usage();
    };

    let files_dir = Path::new(&dir_arg);
    let hash_json_path = Path::new(&json_arg);

    if !files_dir.is_dir() || !hash_json_path.is_file() {
        eprintln!("Argument error: Directory or JSON file does not exist");
        usage();
    }

    let json_content = fs::read_to_string(hash_json_path).unwrap_or_else(|e| {
        eprintln!("ERROR: Failed to read JSON file: {e}");
        process::exit(1);
    });

    let manifest: Manifest = serde_json::from_str(&json_content).unwrap_or_else(|e| {
        eprintln!("ERROR: Failed to parse JSON: {e}");
        process::exit(1);
    });

    if manifest.files.is_empty() {
        eprintln!("ERROR: Hash table JSON is empty");
        process::exit(1);
    }

    let total = manifest.files.len();
    let mut hasher = Sha256::new();
    let mut buf = [0u8; 65536];

    for (idx, (rel_path, info)) in manifest.files.iter().enumerate() {
        let full_path = files_dir.join(rel_path);

        let Ok(metadata) = fs::metadata(&full_path) else {
            eprintln!("\nERROR: Missing file: {}", full_path.display());
            process::exit(1);
        };

        if metadata.len() != info.size {
            eprintln!("\nERROR: Size mismatch on {} - file is NOT original, do NOT execute it", rel_path);
            process::exit(1);
        }

        let Some(expected_bytes) = decode_hex_32(&info.sha256) else {
            eprintln!("\nERROR: Invalid SHA-256 hash in JSON for {}", rel_path);
            process::exit(1);
        };

        print!("\r[{}/{}] Verifying {}...                    ", idx + 1, total, rel_path);
        let _ = io::stdout().flush();

        if !verify_hash(&full_path, &expected_bytes, &mut hasher, &mut buf).unwrap_or(false) {
            eprintln!("\nERROR: Hash mismatch on {} - file is NOT original, do NOT execute it", rel_path);
            process::exit(1);
        }
    }

    println!(
        "\rOK: All {} files verified successfully ({} {}).                    ",
        total,
        manifest.title.as_deref().unwrap_or("package"),
        manifest.version.as_deref().unwrap_or("")
    );
}
