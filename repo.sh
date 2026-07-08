#!/bin/bash
# ==============================================================================
# AI Codebase Packager (Repomix Wrapper)
# Author: Toomey
#
# PURPOSE:
# This script packages the codebase into a single XML file optimized for 
# Large Language Models (LLMs) with large context windows (e.g., Gemini Pro).
# It also generates a tailored architectural review prompt.
# It automatically prefers the local binary (repomix) for speed, but falls 
# back to a Podman/Docker container if it is missing.
#
# HOW TO USE THIS SCRIPT:
# Make the script executable first: chmod +x repo.sh
#
# EXAMPLE 1: Package the Code
#   Command:  ./repo.sh pack
#   Action:   Creates a timestamped XML file containing the code and a 
#             text file containing the AI prompt.
#
# EXAMPLE 2: Package the Code (Forced Container)
#   Command:  ./repo.sh pack --force-container
#   Action:   Bypasses the local binary check and forces the use of Docker/Podman.
#
# EXAMPLE 3: Install Dependencies
#   Command:  ./repo.sh install
#   Action:   Installs Node.js 20 and Repomix globally (requires sudo/root).
#
# AVAILABLE COMMANDS: pack, install
# ==============================================================================

REPODIR="$(cd "$(dirname "$0")" && pwd)"

# --- Configuration ---
PROMPT_FILE="architect_prompt.txt"
#INCLUDE_PATTERNS="**/*.{cc,h,txt}"  Snags a lot
#INCLUDE_PATTERNS="**/*.{c,h,cpp,sh},**/CMakeLists.txt"
INCLUDE_PATTERNS="**/*.{h,cpp,sh},**/CMakeLists.txt"
BUILD_DIR="BUILD"
NODE_IMAGE="docker.io/library/node:20-alpine"

# --- Helper Functions ---

install_repomix() {
    echo "--- 📦 Starting Repomix Installation... ---"
    echo "Note: This requires sudo privileges and is configured for Rocky/Oracle Linux."
    
    # 1. Reset the nodejs module to clear any defaults
    sudo dnf module reset nodejs -y

    # 2. Enable the Node.js 20 stream
    sudo dnf module enable nodejs:20 -y

    # 3. Install Node.js (includes npm and npx)
    sudo dnf install nodejs -y

    # 4. Install Repomix globally
    sudo npm install -g repomix

    echo "--- ✅ Installation complete. ---"
}

generate_pack() {
    local force_container="${1:-false}"

    # 1. Generate a timestamp
    local timestamp
    timestamp=$(date +"%Y-%m-%d_%H%M")
    local output_file="rdm_${timestamp}.xml"

    echo "--- 📝 Generating AI Prompt... ---"
    
    # 2. Create the Prompt File (Heredoc)
    cat <<EOF > "$REPODIR/$PROMPT_FILE"
# ROLE
You are an expert Senior Software Architect with 20+ years of experience specializing in system design, scalable architecture, security, and maintainability.

# CONTEXT & TASK
I will provide you with a codebase packaged as an XML file. 
* Noise Reduction: If you encounter abnormally large files, raw data, or irrelevant directories that bloat the context window without providing analytical value, explicitly suggest that I exclude those specific paths in future XML generations.

# DELIVERABLES
Depending on my prompt, follow one of these two paths:

PATH A: Specific Inquiry
If I ask a targeted question or request a specific feature, focus your entire response strictly on that request. 

PATH B: Architectural Review
If I do not ask a specific question, or if I ask you to "summarize" or "review", provide a structured response covering exactly these four areas:
1. Project Executive Summary: A high-level overview of the system's purpose, the tech stack, and the primary design patterns utilized.
2. Top 5 Critical Issues: Focus strictly on severe Architecture flaws (e.g., tight coupling, SOLID violations), Scalability bottlenecks, and Security vulnerabilities.
3. Code Smells & Technical Debt: Highlight foundational issues, such as "God objects" or unnecessarily complex logic paths.
4. Strategic Recommendations: Provide the top three immediate refactoring priorities necessary for long-term project health.

# CONSTRAINTS & CODING GUIDELINES
* Tone & Focus: Be direct, highly critical, and strictly objective. Prioritize major structural flaws over minor formatting or stylistic nitpicks.
* Preservation: When rewriting or modifying code, preserve existing comments whenever possible.
* Adding Code: If you add new logic, include brief inline comments explaining the purpose and reasoning behind the addition.
* Removing Code: If you remove logic, comment out the old code instead of deleting it, and add a brief inline reason for its deprecation.
* New Files: If generating an entirely new file, strictly use Doxygen-style comments for all classes and methods.
EOF

    echo "--- 🗜️  Packaging Codebase... ---"
    
    # Define ignore pattern based on the BUILD_DIR variable (handles trailing slashes if present)
    local ignore_pattern="**/${BUILD_DIR//\//}/**"

    # 3. Run Repomix (Local or Docker fallback/force)
    if [[ "$force_container" == "false" ]] && command -v repomix &> /dev/null; then
        echo "⚡ Local 'repomix' binary detected. Running fast pack..."
        repomix --include "$INCLUDE_PATTERNS" \
                --ignore "$ignore_pattern" \
                --output="$REPODIR/$output_file" \
                --no-security-check \
                --style markdown \
                --remove-comments \
                --remove-empty-lines \
                --truncate-base64
    else
        if [[ "$force_container" == "true" ]]; then
            echo "🐳 --force-container flag detected. Forcing Docker execution..."
        else
            echo "🐳 Local repomix not found. Falling back to Docker container..."
        fi
        
        # Mounts the local code and uses npx to fetch and run repomix on the fly
        docker run --rm \
            -v "$REPODIR:/tmp/app:z" \
            -w /tmp/app \
            "$NODE_IMAGE" npx repomix --include "$INCLUDE_PATTERNS" \
                                      --output="$output_file" \
                                      --no-security-check \
                                      --style markdown \
                                      --remove-comments \
                                      --remove-empty-lines \
                                      --truncate-base64
    fi

    echo "-------------------------------------------------------"
    echo "✅ DONE!"
    echo "Code Packaged: $output_file"
    echo "Prompt Ready:  $PROMPT_FILE"
    echo "-------------------------------------------------------"
    echo "Step 1: Copy the text inside $PROMPT_FILE"
    echo "Step 2: Upload $output_file to your AI"
    echo "Step 3: Paste the prompt and hit enter."
}

# --- Command Line Argument Parsing ---
case "$1" in
    pack)
        if [[ "$2" == "--force-container" ]]; then
            generate_pack true
        else
            generate_pack false
        fi
        ;;
    install)
        install_repomix
        ;;
    *)
        echo "Usage: ./repo.sh [COMMAND]"
        echo "Commands:"
        echo "  pack [--force-container]  Packages the codebase into an XML file."
        echo "                            Use --force-container to bypass local binary."
        echo "  install                   Installs Node.js 20 and Repomix globally."
        ;;
esac
