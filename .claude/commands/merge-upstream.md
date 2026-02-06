Merge latest upstream AzerothCore changes into our fork.

Upstream: https://github.com/azerothcore/azerothcore-wotlk
Fork: https://github.com/mattes337/azerothcore-wotlk

## Instructions

Perform the following steps in order. Stop and report to the user if any step fails.

### Step 1: Preflight checks

Verify the working tree is clean:
```bash
cd "G:/WoW Projects/.original/server" && git status --porcelain
```
If there are uncommitted changes, stop and ask the user to commit or stash them first.

### Step 2: Ensure upstream remote exists

Check if the `upstream` remote is configured:
```bash
cd "G:/WoW Projects/.original/server" && git remote -v
```
If `upstream` is not listed, add it:
```bash
cd "G:/WoW Projects/.original/server" && git remote add upstream https://github.com/azerothcore/azerothcore-wotlk.git
```

### Step 3: Fetch upstream

```bash
cd "G:/WoW Projects/.original/server" && git fetch upstream
```

### Step 4: Check what's new

Show the user what will be merged before doing it:
```bash
cd "G:/WoW Projects/.original/server" && git log --oneline HEAD..upstream/master | head -20
```
Tell the user how many commits are ahead. If there are no new commits, report that the fork is already up to date and stop.

### Step 5: Merge upstream into local master

Use a real merge commit (not fast-forward) so GitHub records it as a proper merge:
```bash
cd "G:/WoW Projects/.original/server" && git merge upstream/master --no-edit
```

If there are merge conflicts, handle them with confidence-based auto-resolution:

1. List the conflicting files: `git diff --name-only --diff-filter=U`
2. For EACH conflicting file, read the file and examine the conflict markers (`<<<<<<<`, `=======`, `>>>>>>>`)
3. Assess each conflict on a confidence scale:

**Auto-resolve (confidence >= 95%)** -- resolve without asking:
- Our side only adds new files/content that doesn't exist upstream (e.g. `.changes/`, `.claude/`) -- accept OURS
- Upstream changed a file we never touched (no fork-specific modifications) -- accept THEIRS
- Trivial whitespace or formatting-only differences
- Adjacent non-overlapping changes (both sides added different things to different parts of the file)
- Upstream deleted a file we never modified -- accept deletion
- Both sides changed the same lines BUT you can understand the intent of both changes and produce a merged result that satisfies both intents (e.g. upstream fixed a bug and we added a feature nearby -- combine both)
- One side refactored/renamed and the other made a small functional change -- apply the functional change to the refactored version
- Both sides added different entries to the same list, table, enum, or config block -- include both additions

The bar is: you are 95% confident the merged result will compile/work correctly and honors the intent of both sides. Same-line changes are fine to auto-resolve as long as you understand what both sides were doing and can combine them.

**Ask the user (confidence < 95%)**:
- Both sides made conflicting functional changes to the same logic and you cannot combine them without choosing one approach over the other
- Mutually exclusive design decisions (e.g. upstream removed a feature we extended)
- You are unsure about the intent of either side
- The merged result might not compile or might change behavior in an unintended way

For each conflict, report:
- The file path
- What our side changed vs what upstream changed
- Your confidence level and reasoning
- Whether you auto-resolved it or need user input

After resolving all conflicts:
```bash
cd "G:/WoW Projects/.original/server" && git add -A && git commit --no-edit
```

### Step 6: Push to fork

```bash
cd "G:/WoW Projects/.original/server" && git push origin master
```

### Step 7: Summary

Report to the user:
- Number of commits merged
- The merge commit hash
- Confirm the push succeeded
