# Editor run configuration (VS Code or Google Antigravity)

The repository root contains `.vscode/tasks.json` and `.vscode/launch.json`
(copies are kept here for reference). Both start the **packaged** `Launch.exe`;
nothing is compiled and no internet connection is used.

1. File > Open Folder... and select the extracted `CandidateID_Workbench` folder.
2. If asked "Do you trust the authors of the files in this folder?", choose **Yes**
   (VS Code only runs workspace tasks in trusted folders).
3. Either press **Ctrl+Shift+B** (default task "Run Workbench (Launch.exe)"),
   or open Run and Debug and press **F5** on "Run Workbench (packaged)".

Extension prerequisites: **none**. The task uses the built-in task runner and
the launch configuration uses the built-in JavaScript debugger's
`node-terminal` type, which ships with VS Code and Antigravity. Node.js does
not need to be installed: the terminal simply runs `.\Launch.exe`.

Tested: the JSON files were validated and the command they run (`Launch.exe` in
the folder root) is the same one exercised by the standalone launch test. The
editor flow itself must be clicked through once on the target PC (see README,
"Manual checks").
