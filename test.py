import os
import subprocess

def main():
    # Change directory to 'build/tests'
    build_tests_path = os.path.join("Build", "tests")
    if os.path.exists(build_tests_path):
        os.chdir(build_tests_path)
    else:
        print(f"Error: Directory {build_tests_path} not found")
        return

    # Execute the CommonTest binary
    result = subprocess.run(["./CommonTest" if os.name != 'nt' else "CommonTest.exe"], shell=True)

    # Check the return code
    if result.returncode == 0:
        print("Tests executed successfully.")
    else:
        print(f"Tests failed with return code {result.returncode}.")

if __name__ == "__main__":
    main()

