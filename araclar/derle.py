"""Bütün projeleri derler, imzalar ve yayin/ klasörüne koyar (GitHub Actions bunu her değişiklikte çalıştırır; bilgisayarda da çalışır).
Kullanım: python araclar/derle.py [--hepsi] [--proje priz] [--kart c3] [--cikti yayin] [--usb]
  - Varsayılan: 'temel' her kart için + cihazlar.json'da kullanılan (proje, kart) çiftleri. --hepsi: her proje her kart için.
  - İmza: OTA_IMZA ortam değişkeni (PEM özel anahtar) ya da --anahtar dosyası. İmzasız sürüm cihazlar tarafından KURULMAZ.
  - --usb: ilk USB yüklemesi için bootloader/partitions/boot_app0 dosyalarını da koyar.
Çıktı: yayin/cihazlar.txt (kimlik=proje), yayin/<proje>/<kart>.bin + <kart>.txt (surum, boyut, sha256, imza)."""
import argparse
import base64
import hashlib
import json
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

KOK = Path(__file__).resolve().parent.parent


def imzala(veri, pem):
    from cryptography.hazmat.primitives import hashes, serialization
    from cryptography.hazmat.primitives.asymmetric import ec, utils
    k = serialization.load_pem_private_key(pem, password=None)
    h = hashlib.sha256(veri).digest()
    return base64.b64encode(k.sign(h, ec.ECDSA(utils.Prehashed(hashes.SHA256())))).decode()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--hepsi", action="store_true")
    ap.add_argument("--proje")
    ap.add_argument("--kart")
    ap.add_argument("--cikti", default=str(KOK / "yayin"))
    ap.add_argument("--anahtar")
    ap.add_argument("--usb", action="store_true")
    ap.add_argument("--cli", default=os.environ.get("ARDUINO_CLI", "arduino-cli"))
    ap.add_argument("--cli-ayar", default=os.environ.get("ARDUINO_CLI_CONFIG", ""))
    ap.add_argument("--depo", default=os.environ.get("GITHUB_REPOSITORY", "muzafferinonu/jarvis-esp"))
    a = ap.parse_args()

    kartlar = json.loads((KOK / "kartlar.json").read_text(encoding="utf-8"))
    cih = json.loads((KOK / "cihazlar.json").read_text(encoding="utf-8")).get("cihazlar", {})
    projeler = sorted(p.name for p in (KOK / "projeler").iterdir() if p.is_dir() and (p / f"{p.name}.ino").exists())
    for cid, v in cih.items():
        pr = v["proje"] if isinstance(v, dict) else v
        if pr not in projeler:
            sys.exit(f"HATA: cihazlar.json'da {cid} için '{pr}' projesi yok. Var olanlar: {', '.join(projeler)}")

    isler = set()
    if a.proje:
        isler = {(a.proje, k) for k in ([a.kart] if a.kart else kartlar)}
    elif a.hepsi:
        isler = {(p, k) for p in projeler for k in kartlar}
    else:
        isler = {("temel", k) for k in kartlar}
        for v in cih.values():
            if isinstance(v, dict):
                isler.add((v["proje"], v.get("kart", "c3")))
                for k in kartlar:  # kart yanlış yazılmış olabilir: kullanılan projeyi iki kart için de derle (cihaz kendi kartınınkini alır)
                    isler.add((v["proje"], k))
            else:
                for k in kartlar:
                    isler.add((v, k))

    pem = os.environ.get("OTA_IMZA", "").encode() or (Path(a.anahtar).read_bytes() if a.anahtar else b"")
    if not pem:
        print("UYARI: imza anahtarı yok, dosyalar imzasız (cihazlar kablosuz kurmaz; sadece USB/elle yükleme için).")
    surum = int(time.time())
    out = Path(a.cikti)
    out.mkdir(parents=True, exist_ok=True)
    cli = [a.cli] + (["--config-file", a.cli_ayar] if a.cli_ayar else [])
    hata = 0
    for pr, k in sorted(isler):
        print(f"== {pr} / {k}", flush=True)
        tmp = KOK / "build" / f"{pr}_{k}"
        shutil.rmtree(tmp, ignore_errors=True)
        flags = f"-DJARVIS_SURUM={surum}UL"  # kart türü çipten, depo adı JarvisCekirdek.h'den gelir
        r = subprocess.run(cli + ["compile", "-b", kartlar[k]["fqbn"], "--libraries", str(KOK / "lib"), "--build-property", f"compiler.cpp.extra_flags={flags}",
                                  "--output-dir", str(tmp), str(KOK / "projeler" / pr)], capture_output=True, text=True)
        if r.returncode != 0:
            hata += 1
            print(r.stdout[-3000:], r.stderr[-3000:])
            continue
        b = (tmp / f"{pr}.ino.bin").read_bytes()
        d = out / pr
        d.mkdir(parents=True, exist_ok=True)
        (d / f"{k}.bin").write_bytes(b)
        man = [f"proje={pr}", f"kart={k}", f"surum={surum}", f"boyut={len(b)}", f"sha256={hashlib.sha256(b).hexdigest()}"]
        if pem:
            man.append(f"imza={imzala(b, pem)}")
        (d / f"{k}.txt").write_text("\n".join(man) + "\n", encoding="utf-8")
        if a.usb:
            for ek in ("bootloader", "partitions"):
                shutil.copy(tmp / f"{pr}.ino.{ek}.bin", d / f"{k}.{ek}.bin")
        print(f"   {len(b)} bayt, sürüm {surum}{' (imzalı)' if pem else ''}")
    lines = [f"{cid}={(v['proje'] if isinstance(v, dict) else v)}" for cid, v in sorted(cih.items())]
    (out / "cihazlar.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")
    (out / "README.md").write_text("Bu dal otomatik üretilir (araclar/derle.py). Elle değiştirmeyin.\n", encoding="utf-8")
    if hata:
        sys.exit(f"{hata} derleme başarısız")


if __name__ == "__main__":
    main()
