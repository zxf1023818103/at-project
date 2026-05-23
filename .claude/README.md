# Claude Code 项目资产

本目录把 at-project 相关的 Claude Code skill / memory 跟代码一起入库，方便在多台开发机之间同步。

## 内容

- `memory/` — 项目记忆条目（用户偏好、踩过的坑、外部资源引用）
- `skills/ameba-openocd-flash/` — Ameba 系列用 OpenOCD + CMSIS-DAP 烧录的完整 SOP（含 Green2 UARTBURN 解锁等）
- `skills/amebad-imagetool-flash/` — AmebaD 用 UART + MP_ImageTool 烧录

## 在新机器上装上

### Skills

Claude Code 会自动加载 `<project_root>/.claude/skills/` 下的 skills，**无需额外操作**——只要这个目录是仓库的一部分。

### Memory

Claude Code 默认从 `~/.claude/projects/<encoded-path>/memory/` 加载 memory。encoded path 是把项目目录的 `/` 替换成 `-`、去掉 leading `-`。例如 `/home/<user>/at-project` 编码成 `-home-<user>-at-project`。

新机器上 clone 完仓库后做一次同步：

```bash
# 替换 <user> 和路径
PROJECT_DIR="$HOME/at-project"
ENCODED=$(echo "${PROJECT_DIR#/}" | tr '/' '-')
ENCODED="-$ENCODED"
MEMORY_DIR="$HOME/.claude/projects/$ENCODED/memory"

mkdir -p "$MEMORY_DIR"
cp "$PROJECT_DIR/.claude/memory/"*.md "$MEMORY_DIR/"
```

或者干脆用 symlink，每次 `git pull` 之后 memory 自动同步：

```bash
rm -rf "$MEMORY_DIR"
ln -s "$PROJECT_DIR/.claude/memory" "$MEMORY_DIR"
```

## 反向同步（在某台机器上更新了 memory）

如果你在某台机器上让 Claude 改了 memory，但用户级目录是真源，要手动同步回这里：

```bash
cp "$HOME/.claude/projects/$ENCODED/memory/"*.md "$PROJECT_DIR/.claude/memory/"
cd "$PROJECT_DIR" && git add .claude/memory && git commit -m "memory: 更新条目"
```

或者直接用上面 symlink 的方案，省事。
