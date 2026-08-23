# Repository Mirroring / 仓库镜像说明

CosmoEdge maintains a read-only mirror on [Gitee](https://gitee.com/cosmo-wander-ai/cosmo-edge) for users in mainland China.

CosmoEdge 在 [Gitee](https://gitee.com/cosmo-wander-ai/cosmo-edge) 维护只读镜像，方便国内用户访问。

---

## Primary & Mirror / 主从关系

| | GitHub (Primary / 主仓) | Gitee (Mirror / 镜像) |
|---|---|---|
| URL | [github.com/cosmo-wander-ai/cosmo-edge](https://github.com/cosmo-wander-ai/cosmo-edge) | [gitee.com/cosmo-wander-ai/cosmo-edge](https://gitee.com/cosmo-wander-ai/cosmo-edge) |
| Role | Source of Truth (主仓) | Read-only mirror (只读镜像) |
| Sync | — | Auto-sync via GitHub Actions (自动同步) |

## Issues & Pull Requests / 问题与贡献

| Action | Where / 位置 |
|--------|-------------|
| Bug reports (问题反馈) | [GitHub Issues](https://github.com/cosmo-wander-ai/cosmo-edge/issues) 或 [Gitee Issues](https://gitee.com/cosmo-wander-ai/cosmo-edge/issues) |
| Feature requests (功能建议) | [GitHub Issues](https://github.com/cosmo-wander-ai/cosmo-edge/issues) 或 [Gitee Issues](https://gitee.com/cosmo-wander-ai/cosmo-edge/issues) |
| Pull Requests (代码贡献) | [GitHub](https://github.com/cosmo-wander-ai/cosmo-edge/pulls) only |
| Discussions (讨论) | [GitHub Discussions](https://github.com/cosmo-wander-ai/cosmo-edge/discussions) |

> **Note / 说明**: Gitee Issues are monitored, but pull requests should be submitted to GitHub. Code merged on GitHub is automatically mirrored to Gitee.
>
> Gitee Issues 会被关注处理，但代码贡献请提交到 GitHub。合并到 GitHub 的代码会自动同步到 Gitee。

## Sync Frequency / 同步频率

- Triggered on every push to the `main` branch (每次 `main` 分支 push 后自动触发)
- Manual trigger available via `workflow_dispatch` (支持手动触发)
- Typical sync delay: < 2 minutes (同步延迟通常 < 2 分钟)

## README Rendering / README 展示

- GitHub continues to render `README.md` and `README.zh-CN.md`, including its
  native attachment-video players.
- Gitee prioritizes `Readme.osc.md`. That generated file keeps the Chinese
  README content but replaces attachment videos with poster images linked to
  stable playback pages on `www.cosmowander.ai`.
- `Readme.osc.md` is generated from `README.zh-CN.md`; do not edit it directly.
  Run `npm run gitee:readme:generate` after changing the Chinese README, and
  `npm run gitee:readme:check` to verify that it is current.

- GitHub 继续展示 `README.md` 与 `README.zh-CN.md`，保留原生附件视频播放器。
- Gitee 优先展示 `Readme.osc.md`。该生成文件保留中文 README 内容，只把附件视频替换为跳转
  `www.cosmowander.ai` 稳定播放页的封面图。
- `Readme.osc.md` 由 `README.zh-CN.md` 生成，请勿直接编辑。中文 README 更新后运行
  `npm run gitee:readme:generate`，并使用 `npm run gitee:readme:check` 验证一致性。

## For Mainland China Users / 国内用户

If you experience slow access to GitHub, use the Gitee mirror:

如果 GitHub 访问较慢，可使用 Gitee 镜像：

```bash
git clone https://gitee.com/cosmo-wander-ai/cosmo-edge.git
```
