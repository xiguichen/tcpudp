import os
import subprocess

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
compile_project()
