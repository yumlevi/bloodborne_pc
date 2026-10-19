# BBSC 云端节点与 1.8.1 双机排障手册

适用状态：传统在线节点 `100.64.0.1` 保持运行；独立 BBSC 服务端节点已迁移到云端，
仍为原 Headscale Node 2 / `100.64.0.2`。本手册**不要求再次迁移或注册该节点**。
服务器路径和服务名来自 2026-10-18 的交接记录，执行任何更新前先以只读命令核对现状。

## 1. 隔离边界

| 用途 | 私网 / 本地地址 | 云端目标 |
| --- | --- | --- |
| 传统在线 | `100.64.0.1`、客户端 `127.0.0.2` | 原 `bbport-online-remote` Compose，不改 |
| BBSC shadNet 游戏 / WebAPI | `100.64.0.2:31313,31315/tcp`、客户端 `127.0.0.3` | `127.0.0.1:32313,32315/tcp` |
| BBSC STUN | `100.64.0.2:31314/udp` | `127.0.0.1:32314/udp`，需可信 `BBST` 来源 |
| BBSC 游戏 API / 中继 | `100.64.0.2:18080,31316/tcp` | `127.0.0.1:32443,32444/tcp` |
| 玩家间游戏流量 | 各玩家自己的 `100.64.0.x:31316/udp` | 两玩家尾网节点互通；**不是**云端 TCP 中继 |

BBSC Compose 位于 `/opt/bbport-seamless/deploy/compose.yaml`，项目为
`bbport-seamless`，桥配置为 `/opt/bbport-seamless/deploy/server-net.json`。
不要对传统项目 `/opt/bbport-online/deploy/online_remote/compose.yaml` 执行更新，
也不要跨项目复用数据库卷、桥身份卷或运行 `down -v`。

## 2. 云端只读检查（每次部署或测试前）

先登录云端并检查 SSH 是否允许连接；当前自动检查在 SSH banner 前被远端断开，
因此以下输出尚未由本手册验证。命令在服务器本机也可执行：

```sh
docker compose -p bbport-seamless -f /opt/bbport-seamless/deploy/compose.yaml config --quiet
docker compose -p bbport-seamless -f /opt/bbport-seamless/deploy/compose.yaml ps
docker ps --format 'table {{.Names}}\t{{.Status}}'
docker exec bbport-online-remote-headscale-1 headscale nodes list
docker logs --tail 100 bbport-seamless-bridge-1
docker logs --tail 100 bbport-seamless-shadnet-1
systemctl is-active bbport-shadnet-account-watch.service
systemctl is-active bbport-shadnet-account-sync.timer
curl --fail --silent http://127.0.0.1:32315/status
curl --fail --silent --output /dev/null --write-out '%{http_code}\n' http://127.0.0.1:32443/bb-eu/ss.info
```

预期桥日志显示 `ready` 与 `100.64.0.2`；Headscale 同时有独立的 `.1` 和 `.2`，
且两位玩家各有不同的 `100.64.0.x`。健康 HTTP 200 只能证明控制面，不证明玩家
UDP 能通信。检查日志时不要粘贴设备密钥、账号 token、数据库内容或完整配置。

## 3. 已有云端部署的维护步骤

1. 保存当前独立 Compose、桥配置、镜像标签、三个 BBSC 卷的备份，并记录
   `docker compose ... ps` 与 Headscale Node 2 的 ID/IP；不要启动封存于本地的
   旧 `tailscaled.state`，不能让两处同时使用一个节点身份。
2. 只把经测试的新 BBSC 镜像或配置放进 `/opt/bbport-seamless`。仓库中的
   `deploy/seamless_lab/compose.yaml`、`compose.overlay.yaml` 和
   `server-net.remote.json` 是可审查的参考；云端实际 Compose 与它们可能不同，
   不要盲目覆盖。更新前先用上面的 `config --quiet` 校验合并结果，确认发布端口仍
   只绑定 `127.0.0.1`、桥仍用独立卷、`SHADNET_SEAMLESS=true`、
   `BBPORT_TRUST_TAILNET_STUN=1`、`SEAMLESS_ADVERTISE_IP=127.0.0.3`。
3. 仅在安排维护窗口并准备好回滚镜像后，对 `bbport-seamless` 项目执行明确的
   `docker compose up -d` 更新；不要连带重建传统项目。更新后复查第 2 节的
   容器、IP、HTTP 和 watcher。既有账号镜像脚本/数据库在本次 UDP 排障中不修改。
4. 1.8.1 **只更新 Windows 启动器和内置私网客户端**；本次不要求部署新云端
   Docker 镜像，也不要求更改 `.2` 桥配置、Headscale ACL 或公开云端后端端口。

## 4. 两台 Windows 客户端安装与验证

1. 两端保留 v1.7.20 运行目录及存档备份，使用同一份
   `Bloodborne-PC-Online-v1.8.1.exe` 安装到各自**新的**运行目录。
   不复制另一台电脑的 `%LOCALAPPDATA%\BloodbornePC\embedded-net\identity`。
2. 两端各使用独立的联机 ID、账号和尾网身份。启动器连接私网后分别记录分配的
   `100.64.0.x`，选择实验性 BBSC；普通在线仍应通过 `.1` 服务正常启动。
3. 同时启动游戏：一端摇共鸣铃，另一端摇召唤铃，保持 60 秒以上。对齐两端的
   `logs/run-*.log`、`runtime/bbsc/logs/bbsc-mod-*.log` 以及新生成的
   `logs/net-traffic-*.log`。记录召唤发生时间和哪台是主机。
4. 新日志中的“总发出/总收到”包括 STUN；诊断玩家路径看“玩家发出/玩家收到”
   （UDP 31316）及“发往/来自”的私网 IP。A 的玩家发出增加而 B 的玩家收到不变，
   排查 A→B 尾网传输；反向同理。双方玩家收发均增加却仍无 JoinRoom，则重点查
   游戏信令与 BBSC 会话阶段。若玩家发出始终为零，先查 STUN/房间信令是否启动。
5. 先验收同 LAN，再验收不同 LAN。成功标准是实际 JoinRoom、看见对方并稳定
   游玩，不是“已连接私网”或 WebAPI 200。换图、死亡、Boss、重连需单独回归。

## 5. 回滚

- 客户端：关掉游戏和启动器，改用保留的 v1.7.20 运行目录；不删除存档或尾网
  身份。若要恢复原安装位置，先明确备份该目录，再重装旧版。
- 云端：本次 1.8.1 不修改服务端。若后续独立 BBSC 更新失败，只在确认卷备份、
  原镜像标签与配置后回滚 **`bbport-seamless`**；传统项目和 `.1` 不参与。
- 不删除 Node 2、不重新注册 `.2`、不运行 `docker compose down -v`。
