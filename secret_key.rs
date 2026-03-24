/*
	char ascii_key[sizeof secret_key << 1];
	for (size_t i = 0; i < sizeof secret_key; ++i) {
		ascii_key[i] = (secret_key[i] >> 4) | 0x40;
		ascii_key[i + sizeof secret_key] = ((secret_key[i] >> 4) ^ (secret_key[i] & 0x0F)) | 0x40;
	}
	stream->print("SECRET_KEY=");
	for (size_t i = 0; i < sizeof ascii_key; ++i)
		stream->print(ascii_key[i]);
	stream->println();
*/

use ::std::env::args;

const KEY_SIZE: usize = 16;

fn main() {
	let mut p = args();
	let Some(prog) = p.next() else {return};
	match p.next() {
		Some(cmd) if cmd == "e" => {
			let Some(k) = p.next() else {
				eprintln!("ERROR: secret key is missing");
				return
			};
			print!("Encoded upper: ");
			let k: Vec<u32> = k.chars().map(u32::from).collect();
			for i in 0 .. k.len() {
				print!("{}", unsafe {char::from_u32_unchecked(k[i] >> 4 & 0xFF | 0x40)})
			};
			println!();
			print!("Encoded lower: ");
			for i in 0 .. k.len() {
				print!("{}", unsafe {char::from_u32_unchecked((k[i] >> 4 ^ k[i] & 0x0F) & 0xFF | 0x40)})
			};
			println!()
		},
		Some(cmd) if cmd == "d" => {
			let Some(k) = p.next() else {
				eprintln!("ERROR: secret key is missing");
				return
			};
			let mut a = [0u32; KEY_SIZE];
			let k: Vec<u32> = k.chars().map(u32::from).collect();
			for i in 0 .. k.len() {
				let ik = i % KEY_SIZE;
				a[ik] = a[ik] ^ (a[ik] << 4 & 0xFF) ^ k[i]
			};
			print!("Decoded binary:");
			for i in 0 .. KEY_SIZE {
				print!(" {:02X}", a[i])
			};
			println!();
			print!("Decoded text: ");
			for i in 0 .. KEY_SIZE {
				print!("{}", unsafe {char::from_u32_unchecked(a[i] & 0xFF)})
			};
			println!()
		},
		_ => {
			println!("Usage:");
			println!("\t{} e {{secret key}} # encode secret key", prog);
			println!("\t{} d {{secret key}} # decode secret key", prog);
			println!("\t{} help             # print this help message", prog);
		}
	}
}
