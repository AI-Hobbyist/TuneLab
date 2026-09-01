## Tool Selection Strategy & Constraints

1. **Prioritize CodeGraph / Precision Retrieval**:
   - ALWAYS search for definitions, references, implementation, context, and **repository structure/architecture** using CodeGraph / Symbol Navigation first.
   - For high-level or goal-driven tasks (e.g., `/goal`, "写计划书", "分析整个项目", "架构勘察"):
     - **DO NOT** trigger `explore` or repo-wide search to write plans or analyze architecture.
     - FIRST query CodeGraph at the project root (`projectPath`) or look up top-level entry points (e.g., `main`, `export`, `InferenceEngine`, build files) to map graph topology.
   - DO NOT invoke broad file exploration (`explore`, repo-wide semantic search, directory listing, or fuzzy file navigation) for ANY reason unless CodeGraph explicitly returns empty results or unindexed project state.

2. **Token & Latency Efficiency**:
   - Treat `explore` tools as high-cost fallback actions.
   - Limit the context window to active editor files, project-root CodeGraph responses, and explicitly tagged symbols (`#file`, `#symbol`).

3. **Execution Workflow**:
   - Step 1 (Goal/Plan/Survey): Query CodeGraph at `projectPath` or locate entry symbols for key modules (e.g., export/inference/model structures).
   - Step 2: Read ONLY the target files/modules returned by CodeGraph graph traversal.
   - Step 3 (Strict Fallback): Run `explore` tools IF and ONLY IF precision graph lookup fails or CodeGraph indicates the repository is unindexed.

## Command Execution & Terminal Output Rules

4. **No Background & No Log Redirection**:
   - NEVER execute compilation, build, or test commands in the background.
   - NEVER capture, parse, or suppress compilation outputs using `$log`, external log files, or streaming wrappers.
   - NEVER hide build progress behind silent spinners or buffered scripts.

5. **Foreground & Real-Time Printing**:
   - ALL compilation, build, and execution commands MUST run directly in the interactive foreground terminal.
   - Force unbuffered, verbose, and real-time output stream (`stdout` and `stderr`) so the progress is immediately visible line-by-line.
   - Always append flags for verbose execution where applicable (e.g., `--verbose`, `-v`, `--progress`).

## SDKS
- **Qt6:** `C:\Qt\6.10.3`
- **Windows SDK:** `D:\Windows Kits\10`