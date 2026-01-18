import os
import platform
import subprocess

def main():
    # Navigate to the build/bin directory
    os.chdir(os.path.join("build", "bin"))

    # Determine the appropriate client to run based on the platform
    if platform.system() == "Windows":
        subprocess.run(["udp_client.exe"], check=True)
    else:
        subprocess.run(["./udp_client"], check=True)

if __name__ == "__main__":
    main()

