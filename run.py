import os
import platform
import subprocess
import shutil
import sys

def main():
    # Source and destination directories
    build_dir = os.path.join("Build", "bin")
    run_dir = "run"
    client_name = "udp_client.exe" if platform.system() == "Windows" else "udp_client"

    # Create run directory if it doesn't exist
    if not os.path.exists(run_dir):
        os.makedirs(run_dir)
        print(f"Created directory: {run_dir}")

    # Copy client from build to run directory
    source_client = os.path.join(build_dir, client_name)
    dest_client = os.path.join(run_dir, client_name)

    if not os.path.exists(source_client):
        print(f"Error: Client not found at {source_client}")
        print("Please run 'python3 build.py' first to build the project.")
        sys.exit(1)

    try:
        shutil.copy2(source_client, dest_client)
        print(f"Copied client from {source_client} to {dest_client}")
    except Exception as e:
        print(f"Error copying client: {e}")
        sys.exit(1)

    # Change to run directory and execute
    os.chdir(run_dir)

    try:
        if platform.system() == "Windows":
            subprocess.run([client_name], check=True)
        else:
            subprocess.run(["./" + client_name], check=True)
    except subprocess.CalledProcessError as e:
        print(f"Client exited with error: {e}")
        sys.exit(1)
    except KeyboardInterrupt:
        print("\nClient interrupted by user")
        sys.exit(0)

if __name__ == "__main__":
    main()

