import os
import subprocess

def main():
    # Change directory to 'build/tests'
    os.chdir(os.path.join("build", "tests"))

    # Execute the CommonTest binary
    result = subprocess.run(["./CommonTest" if os.name != 'nt' else "CommonTest.exe"], shell=True)

    # Check the return code
    if result.returncode == 0:
        print("Tests executed successfully.")
    else:
        print(f"Tests failed with return code {result.returncode}.")

if __name__ == "__main__":
    main()

