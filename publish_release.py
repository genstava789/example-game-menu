import os
import sys
import subprocess
import json
import urllib.request
import urllib.error

REPO = "genstava789/example-game-menu"
TAG = "v1.0.0"
APK_FILE = "ModMenu_Standalone_arm64.apk"

def main():
    token = os.environ.get("GITHUB_TOKEN")
    if not token and len(sys.argv) > 1:
        token = sys.argv[1].strip()

    if not token:
        print("[!] Error: GitHub Personal Access Token (PAT) required.")
        print("Usage: python publish_release.py <YOUR_GITHUB_TOKEN>")
        sys.exit(1)

    # 1. Update remote URL with token and push
    print("[*] Pushing commits and tags to GitHub...")
    auth_remote = f"https://{token}@github.com/{REPO}.git"
    res = subprocess.run(["git", "push", "-u", auth_remote, "main", "--tags"], capture_output=True, text=True)
    if res.returncode != 0:
        print(f"[!] Git push failed:\n{res.stderr}\n{res.stdout}")
        sys.exit(1)
    print("[+] Git push successful!")

    # Reset remote URL to clean URL (without token)
    subprocess.run(["git", "remote", "set-url", "origin", f"https://github.com/{REPO}.git"])

    # 2. Create Release via GitHub API
    print(f"[*] Creating GitHub Release for {TAG}...")
    create_url = f"https://api.github.com/repos/{REPO}/releases"
    headers = {
        "Authorization": f"token {token}",
        "Accept": "application/vnd.github.v3+json",
        "User-Agent": "ReleasePublisher"
    }
    payload = {
        "tag_name": TAG,
        "name": f"Subway Surfers Mod Menu {TAG}",
        "body": "### Features Included:\n"
                "- **Add Coins**: Toggle adding 50,000 coins.\n"
                "- **Speed Multiplier**: Slider 1-50x multiplier.\n"
                "- **Jump Multiplier**: Natural height scaling slider 1-10x.\n"
                "- **Add Keys**: Manual input with Apply button and instant real-time visual UI update.\n"
                "- **Magnet**: Toggle coin/item attraction with automatic activation on game start/restart.\n\n"
                "**Artifact**: `ModMenu_Standalone_arm64.apk` (Signed ARM64 standalone APK)",
        "draft": False,
        "prerelease": False
    }

    req = urllib.request.Request(create_url, data=json.dumps(payload).encode("utf-8"), headers=headers, method="POST")
    try:
        with urllib.request.urlopen(req) as resp:
            data = json.loads(resp.read().decode("utf-8"))
            release_id = data.get("id")
            upload_url_template = data.get("upload_url")
            print(f"[+] Release created successfully (ID: {release_id})")
    except urllib.error.HTTPError as e:
        error_msg = e.read().decode("utf-8")
        # If release already exists, fetch it
        if e.code == 422:
            print("[*] Release might already exist, fetching existing release...")
            get_req = urllib.request.Request(f"https://api.github.com/repos/{REPO}/releases/tags/{TAG}", headers=headers)
            with urllib.request.urlopen(get_req) as resp:
                data = json.loads(resp.read().decode("utf-8"))
                release_id = data.get("id")
                upload_url_template = data.get("upload_url")
                print(f"[+] Found existing release (ID: {release_id})")
        else:
            print(f"[!] Failed to create release: {e.code} - {error_msg}")
            sys.exit(1)

    # 3. Upload APK binary asset
    if not os.path.exists(APK_FILE):
        print(f"[!] Error: {APK_FILE} not found!")
        sys.exit(1)

    apk_size = os.path.getsize(APK_FILE)
    print(f"[*] Uploading {APK_FILE} ({apk_size:,} bytes) to GitHub Release...")
    upload_url = f"https://uploads.github.com/repos/{REPO}/releases/{release_id}/assets?name={APK_FILE}"
    upload_headers = {
        "Authorization": f"token {token}",
        "Content-Type": "application/vnd.android.package-archive",
        "User-Agent": "ReleasePublisher",
        "Content-Length": str(apk_size)
    }

    with open(APK_FILE, "rb") as f:
        apk_data = f.read()

    upload_req = urllib.request.Request(upload_url, data=apk_data, headers=upload_headers, method="POST")
    try:
        with urllib.request.urlopen(upload_req) as resp:
            asset_info = json.loads(resp.read().decode("utf-8"))
            browser_download_url = asset_info.get("browser_download_url")
            print(f"[+] APK uploaded successfully!")
            print(f"[+] Download URL: {browser_download_url}")
    except urllib.error.HTTPError as e:
        print(f"[!] Failed to upload asset: {e.code} - {e.read().decode('utf-8')}")
        sys.exit(1)

    print("\n" + "=" * 60)
    print(f"  RELEASE PUBLISHED SUCCESSFULLY!")
    print(f"  Repository: https://github.com/{REPO}")
    print(f"  Release:    https://github.com/{REPO}/releases/tag/{TAG}")
    print("=" * 60)

if __name__ == "__main__":
    main()
