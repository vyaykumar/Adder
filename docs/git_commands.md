# Git CLI Reference & Command Rundown

A quick, direct-copy reference of essential Git commands for daily development, debugging, history navigation, and recovery.

---

## 1. Setup & Configuration

Configure identity, defaults, and line ending handling across platforms:

```bash
# Set global identity
git config --global user.name "Your Name"
git config --global user.email "your.email@example.com"

# Set default initial branch name to main
git config --global init.defaultBranch main

# Line ending handling (Windows: crlf conversion; Linux/macOS: input)
git config --global core.autocrlf true       # Windows
git config --global core.autocrlf input      # Linux / macOS

# Useful aliases for cleaner logs and fast status
git config --global alias.lg "log --graph --pretty=format:'%Cred%h%Creset -%C(yellow)%d%Creset %s %Cgreen(%cr) %C(bold blue)<%an>%Creset' --abbrev-commit"
git config --global alias.s "status -s"

# Inspect active configuration
git config --list --show-origin
```

---

## 2. Starting & Cloning Repositories

```bash
# Initialize a new local repository in current directory
git init

# Clone repository via SSH or HTTPS
git clone https://github.com/user/repo.git
git clone git@github.com:user/repo.git

# Clone into a specific folder name
git clone git@github.com:user/repo.git my-folder

# Clone single branch or shallow depth (fast download for large repos)
git clone --depth 1 --branch main git@github.com:user/repo.git
```

---

## 3. Daily Workflow: Inspect, Stage & Commit

```bash
# Check working tree status (verbose & concise)
git status
git status -s

# View differences
git diff                     # Unstaged modifications vs working tree
git diff --staged            # Staged changes vs last commit (alias: --cached)
git diff HEAD~1 HEAD         # Compare previous commit with latest commit

# Stage changes
git add <file>               # Stage specific file
git add .                    # Stage all tracked and untracked changes
git add -p                   # Interactively stage hunks / patches

# Commit changes
git commit -m "feat: concise summary"
git commit -am "fix: add tracked files and commit in one step"

# Amend the most recent commit (combine staged edits or update message)
git commit --amend           # Opens editor to modify commit message
git commit --amend --no-edit # Modifies last commit with staged files, keeps message
```

---

## 4. Branch Management & Switching

```bash
# List branches
git branch                   # List local branches (* indicates active branch)
git branch -a                # List local and remote tracking branches
git branch -vv               # List local branches with upstream tracking and commit hash

# Create and switch branches (modern syntax: git switch)
git switch <branch-name>          # Switch to an existing branch
git switch -c <new-branch>        # Create and switch to new branch (alias: checkout -b)
git switch -                      # Switch back to the previous branch

# Classic checkout syntax
git checkout -b <new-branch>      # Create and switch
git checkout <branch-name>        # Switch branch

# Rename branch
git branch -m <new-name>          # Rename current branch
git branch -m <old-name> <new-name>

# Delete branches
git branch -d <branch-name>       # Delete branch safely (fails if unmerged)
git branch -D <branch-name>       # Force delete branch regardless of merge status
git push origin --delete <branch> # Delete branch from remote
```

---

## 5. Remote Syncing: Fetch, Pull & Push

```bash
# Remote repository inspection
git remote -v
git remote add origin git@github.com:user/repo.git
git remote set-url origin git@github.com:user/repo.git

# Fetch changes from remote without merging
git fetch origin
git fetch --prune origin          # Fetch and remove stale remote-tracking references

# Pull changes (fetch + merge / rebase)
git pull origin <branch>
git pull --rebase origin <branch> # Recommended: replay local commits on top of remote

# Push changes
git push -u origin <branch>       # Push and link local branch to upstream tracking
git push                          # Push on already tracked branch
git push --force-with-lease       # Safe force-push (refuses push if remote moved)
```

---

## 6. Merging & Rebasing

```bash
# Merge a feature branch into the current branch
git merge <feature-branch>
git merge --no-ff <feature-branch> # Create merge commit even if fast-forward is possible
git merge --abort                  # Abort merge in case of unresolvable conflicts

# Rebase current branch onto another branch (e.g., rebase feature onto main)
git rebase main
git rebase -i HEAD~3               # Interactive rebase of last 3 commits (squash, fixup, edit, drop)

# Handling rebase conflicts
git status                         # Shows conflicting files
# (resolve conflict markers in files, then stage)
git add <resolved-file>
git rebase --continue
git rebase --abort                 # Abort rebase and restore original state
git rebase --skip                  # Skip current patch
```

---

## 7. Stashing: Pause & Resume Work

```bash
# Save uncommitted changes to stash
git stash
git stash push -m "WIP: feature description"
git stash -u                       # Include untracked files (-u / --include-untracked)

# View stashes
git stash list
git stash show -p stash@{0}        # View diff of a specific stash

# Apply stashes
git stash pop                      # Apply top stash and remove from stash list
git stash apply stash@{0}          # Apply specific stash and keep it in stash list

# Remove stashes
git stash drop stash@{0}           # Remove specific stash entry
git stash clear                    # Remove all stashes
```

---

## 8. Undoing Changes & Recovery ("Life Savers")

```bash
# Discard uncommitted changes in working directory
git restore <file>                 # Discard modifications in specific file
git restore .                      # Discard modifications in all tracked files

# Unstage files without losing modifications
git restore --staged <file>        # Remove file from staging area (modern)
git reset HEAD <file>              # Remove file from staging area (classic)

# Undo commits locally
git reset --soft HEAD~1            # Undo last commit; keep changes staged
git reset HEAD~1                   # Undo last commit; keep changes in working tree (mixed)
git reset --hard HEAD~1            # DANGER: Discard last commit and all changes completely

# Safely revert a commit that is already pushed (creates a new inverse commit)
git revert <commit-hash>
git revert --no-commit <hash>      # Revert changes into working directory without committing

# Clean untracked files and directories
git clean -nd                      # Dry-run: show what untracked files will be removed
git clean -fd                      # Force delete untracked files and directories

# Reflog: Recover "lost" commits, branches, or aborted rebases
git reflog                         # View full history of HEAD movements
git reset --hard HEAD@{2}          # Jump back to state recorded at reflog index 2
```

---

## 9. Inspection, Logs & Blame

```bash
# Commit log visualizers
git log --oneline                  # Compact single-line summary
git log -n 5                       # View last 5 commits
git log --graph --oneline --all    # Terminal visualization of branch graph
git log -p <file>                  # Commit history with line diffs for a file
git log --stat                     # Commits with file change statistics
git log --grep="memory"            # Search commit messages for keyword
git log -S "ibv_reg_mr"            # Search commits that added/removed string (pickaxe)

# Inspect specific commits or objects
git show <commit-hash>             # Show commit diff and metadata
git show <commit-hash>:<path>      # Show contents of a file at specific commit

# Line-by-line attribution
git blame <file>
git blame -L 10,50 <file>          # Blame lines 10 through 50 of file
```

---

## 10. Tagging & Releases

```bash
# List tags
git tag
git tag -l "v1.*"

# Create tags
git tag v1.0.0                     # Lightweight tag
git tag -a v1.0.0 -m "Release 1.0.0" # Annotated tag with message

# Push tags to remote
git push origin v1.0.0             # Push specific tag
git push origin --tags             # Push all local tags

# Delete tags
git tag -d v1.0.0                  # Delete local tag
git push origin --delete v1.0.0    # Delete remote tag
```

---

## 11. Cherry-Picking & Bisecting

```bash
# Apply a specific commit from another branch onto current HEAD
git cherry-pick <commit-hash>
git cherry-pick -n <commit-hash>   # Stage cherry-picked changes without auto-committing
git cherry-pick --abort            # Abort cherry-pick on conflict

# Binary search to find the commit that introduced a bug
git bisect start
git bisect bad                     # Mark current HEAD as broken
git bisect good <known-good-hash>  # Mark previous working commit
# (Git checks out intermediate commits; compile and test)
git bisect good                    # Tell Git this commit works
git bisect bad                     # Tell Git this commit is broken
git bisect reset                   # Finish bisect and return to original HEAD
```

---

## 12. Submodules & Worktrees

```bash
# Submodule management
git submodule update --init --recursive  # Clone and checkout all submodules
git submodule foreach git pull origin main

# Git worktrees (work on multiple branches concurrently without cloning)
git worktree list
git worktree add ../Adder-feature feat/D2-AllReduce  # Checkout branch in sibling directory
git worktree remove ../Adder-feature
```
