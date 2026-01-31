import os
import subprocess

def ensure_build_directory():
    build_dir = "Build"
    if not os.path.exists(build_dir):
        try:
            os.makedirs(build_dir)
            print(f"Created directory: {build_dir}")
        except Exception as e:
            print(f"Could not create {build_dir}: {e}")

def compile_project():
    # Change directory
    try:
        os.chdir("src")  # Replace with your actual directory
    except FileNotFoundError:
        print("Error: Directory not found.")
        return
    except Exception as e:
        print(f"Error: {e}")
        return

    # Run the compile command
    try:
        subprocess.run(["make", "compile"], check=True, text=True )
    except subprocess.CalledProcessError as e:
        print("Compilation failed:")
        print(e.stderr)
    except Exception as e:
        print(f"An error occurred: {e}")

# Call the function
ensure_build_directory()
compile_project()
