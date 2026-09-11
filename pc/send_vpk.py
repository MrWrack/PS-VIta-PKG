#!/usr/bin/env python3
import argparse, os, socket, struct, sys
MAGIC = 0x56504B31

def main():
    ap = argparse.ArgumentParser(description="Send a VPK directly to VPK Manager on PS Vita")
    ap.add_argument("vita_ip", help="PS Vita IP shown in VPK Manager")
    ap.add_argument("vpk", help="Path to .vpk file")
    ap.add_argument("--port", type=int, default=1338)
    args = ap.parse_args()
    if not os.path.isfile(args.vpk):
        sys.exit("File not found: " + args.vpk)
    name = os.path.basename(args.vpk).encode("utf-8")
    size = os.path.getsize(args.vpk)
    if len(name) >= 240 or size <= 0:
        sys.exit("Invalid filename or empty VPK")
    header = struct.pack("<IIQ", MAGIC, len(name), size)
    with socket.create_connection((args.vita_ip, args.port), timeout=15) as s:
        s.sendall(header); s.sendall(name)
        sent = 0
        with open(args.vpk, "rb") as f:
            while True:
                chunk = f.read(256 * 1024)
                if not chunk: break
                s.sendall(chunk); sent += len(chunk)
                print(f"\rSending: {sent*100//size:3d}%", end="", flush=True)
        ok = s.recv(1)
        print()
        if ok != b"\x01": sys.exit("Vita reported an upload error")
    print("Upload complete. VPK Manager will install it automatically.")
if __name__ == "__main__": main()
