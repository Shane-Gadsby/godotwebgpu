# 51-web-start-button: Start button and "preload before run" in the Web export options (TASKS.md §2.51)

State: open
Owner: worker-51
Claimed:
Needs: build (editor + web template), gpu (browser checks)
Branch: work/51
Results: webgpu_notes/work/results/51-web-start-button/

Do TASKS.md §2.51 as written there. Task 42 (§4.56, results/42-web-loading-bar/) just added the progress bar options in
the same place: follow its pattern (export options with visibility, a sibling placeholder for custom shells, both default
shells, default export unchanged and checked by diffing the HTML and comparing renders). Questions for the user (e.g.
whether the button shows without preload) go in for-tasks.md with your default.
