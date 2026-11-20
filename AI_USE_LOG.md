# AI use log

This log is meant to be read literally. It does not understate how much of the
work the AI did.

## 1. Tools and models

| Tool | Model / version | Used for |
|---|---|---|
| Claude, Cowork mode in the Claude desktop app (Anthropic) | configured model id `claude-opus-5-5` | Planning, all C++ and JS source code, tests, build and packaging scripts, documentation drafts, running the builds/tests/benchmarks, and driving the Windows launch test on the candidate's laptop through the app's computer-use feature (with the candidate's permission) |

No paid solver, CAD tool or other AI assistant was used for this submission.
Session: 2026-11-01 17:23 UTC to about 19:00 UTC (22:53 to about 00:30 IST on
1 to 2 November), one continuous Cowork session of roughly 1.5 hours of AI work.
Candidate effort (prompt writing, review, handwritten appendix, final checks):
to be recorded by the candidate in Section 8.

## 2. Division of work (honest summary)

* **Candidate**: supplied the assignment PDF and a detailed
  engineering prompt (Section 3), set the deadline, approved access to her Downloads
  folder and computer use for the Windows test, and is responsible for reviewing,
  understanding and defending the result. She must handwrite the Task F derivation
  herself (Section 7).
* **Claude**: wrote the code, tests, scripts and documents in this repository and
  ran every build, test and measurement reported here. Commits made during the
  session carry a `Co-Authored-By: Claude` trailer.

## 3. Prompts (representative, verbatim excerpts)

Prompt 1 (candidate, start of session; long structured prompt, excerpts):

> "You are an elite multidisciplinary engineering team operating as ONE highly
> capable autonomous AI engineering agent. [...] Your mission is to independently
> design, implement, test, validate, document and package an exceptional engineering
> software application that satisfies the attached PASS SCIENCE WORKS R&D Internship
> 2026–2027 assignment."

> "Create an internal requirements-traceability matrix [...] Do not start by
> generating random code."

> "Prefer a C++ desktop architecture using Qt 6 if the environment supports it."

> "IMPORTANT: Do not calculate error using only nodal values. Use FE interpolation at
> element midpoints as explicitly required by the assignment."

> "Never fabricate handwritten photographs or claim that I personally completed
> handwriting that I have not done."

> "Never invent test results or performance measurements. [...] Do not claim
> clean-machine compatibility without actually testing it."

> "If no unsuitable suggestion occurs, perform and document a deliberate challenge
> test."

Prompt 2 (candidate, mid-session):

> "Can we do it in 10 hours? Because i need to submit by 10am tomorrow"

Claude's reply (excerpt) re-prioritised the plan: launch gate first, then C++ backend
and tests, then thermal FEA (the shortlisting gates), then the remaining tasks,
documentation last, and listed the manual steps only the candidate can do.

## 4. Representative outputs

* Architecture decision sent to the candidate before coding: "C++17 core library + C++
  local HTTP server (cpp-httplib) + an offline web UI (three.js for 3D, Chart.js for
  plots) embedded inside a single statically linked Launch.exe [...] I'm not using
  Qt 6: cross-building and lawfully deploying Qt for Windows from this environment is
  much riskier for the launch gate."
* FEA convergence output of `workbench_cli fea` (first run):
  `N=10 max|midpoint error| = 1.25 C max|nodal error| = 2.84e-14 C`, observed order
  `2.0000000000000657` for N = 20.
* Native Windows benchmark console output (target laptop):
  `p=4 median 0.0315404 s, speedup 3.6336, efficiency 0.9084, max|dT| 0 C`.

## 5. How AI output was evaluated and validated

Nothing below was accepted because the AI said so; each item has an executable check.

| Claim | Independent check |
|---|---|
| FE solver correct | 2-element K and F checked against hand calculation; tridiagonal solve compared with a separate dense Gaussian elimination; exact solution re-derived; midpoint error compared with the closed form q h²/(8k); energy balance; numbers re-computed in a separate Python session outside the application |
| Gradients correct | central finite differences in C++ (ε = 1e-6) and a separate Python check of the forward/backward values |
| Parallel code correct | bitwise agreement with the serial loop for 1 to 4 workers; ordered output test; cancellation test |
| Parsers robust | 19 positive/negative parser tests; real size-limit uploads up to 70 MB |
| Package launches | Windows exe run under Wine in the sandbox **and** natively on the candidate's Windows 11 laptop (double click in Explorer, tests 62/62, benchmark) |
| UI works | Playwright walkthrough with 17 steps and zero browser console errors |

## 6. Incorrect or unsuitable AI suggestions identified (all real, from this session)

1. **Unsuitable stack suggestion (in the prompt).** The prompt preferred Qt 6. Claude
   judged it unsuitable here: no Windows Qt toolchain in the Linux build sandbox,
   heavy DLL deployment, and the launch gate is pass/fail. Chosen instead: static
   MinGW exe with an embedded web UI (explicitly allowed by the brief). The choice
   was validated by the native Windows double-click test.
2. **Wrong number in a test.** Claude wrote the hand-rounded value dJ/dw ≈ −1.209470
   into a test with a loose tolerance. The C++ result and a Python re-calculation give
   −1.209464. The tolerance had hidden the error; the value was corrected and the
   tolerance tightened to 1e-6.
3. **Wrong numbers in the backprop reference sheet.** The post-update h and ŷ were
   first typed from mental arithmetic as 0.630447 and 0.829042. A Python check gave
   0.630287 and 0.828869. Corrected before the PDF was produced.
4. **Broken UI button.** The first Performance view named an input `elements`, which
   shadows the browser's `HTMLFormElement.elements`; the Run button silently failed.
   Found by the end-to-end walkthrough, fixed by renaming.
5. **Route order bug (caught in self-review before the first run).** The static
   file catch-all `GET /(.*)` was first registered before the API routes, which would
   have served 404s for every `GET /api/...`. Moved to the end.
6. **HTTP upload cap.** Size-limit testing with curl returned an empty 413 for a 13 MB
   STL: the HTTP library caps form-urlencoded bodies at 8 KiB. The UI was unaffected
   (it sends text/plain) but the backend now accepts any content type up to 64 MiB and
   returns a JSON message for 413.
7. **Port sharing bug in the generated server code.** The first server relied on
   the HTTP library's default socket options. A second `Launch.exe` on the target PC
   bound the same port as the first (SO_REUSEADDR/SO_REUSEPORT), and the bind-retry
   loop could not move on after a failure. Found during the native re-test of the
   final package; fixed and re-tested with two simultaneous instances.
8. **Compile error from a generated test.** doctest tried to print enum values through
   the ADL-visible `hw::toString()` and failed to compile; comparisons were wrapped.
9. **Misleading benchmark interpretation avoided.** On the 2 vCPU cloud VM the
   1-worker pool was 13 % slower than the serial loop. Rather than claim a fixed
   "pool overhead", the run-to-run spread was examined (0.18 to 0.23 s) and the
   report says the difference is mostly noise; the native Windows run shows 0.7 %.

Challenge test (deliberate): the requirement "error at midpoints, not only at nodes"
was tested by also computing the nodal error. It is about 1e-13 °C for every mesh,
i.e. a nodal-only check would wrongly report a perfect solver. Both columns are kept
in the results and plots to show this.

## 7. What the candidate still has to do personally

* Handwrite the Task F derivation (max 2 pages), scan/photograph it, and add it as
  `docs/report/handwritten_appendix/` (the AI did not and must not produce this).
  `docs/methodology/backprop_derivation_sheet.pdf` is only a reference to check
  against.
* Read the code paths named in `docs/architecture/ARCHITECTURE.md` well enough to
  explain a chosen function, rerun a test and make a small change in the interview.
* Add your own entries below if you change anything after this session.

## 8. Candidate edits after the AI session

| Date | What changed | Why |
|---|---|---|
| | | |
