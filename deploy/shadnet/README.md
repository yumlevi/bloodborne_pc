# bbport 在线联机服务端（第一阶段）

v1.7.1 的公共在线入口、本地隔离 Docker 测试栈、Tailscale 测试栈和
一次性注册码流程请先看 [在线联机本地测试](../../docs/public_online.md)。
下文是此前的局域网 `compose.desktop.yaml`/macvlan 实验说明，两套容器和
数据卷互不迁移，也不要使用 `down -v` 删除任何已有账号数据。

这个目录把 [Wozzardman/shadnet-p2p](https://github.com/Wozzardman/shadnet-p2p) 的
`5a849a192fd4450593c57b10697316ac324c856e` 源码编译进 Ubuntu 24.04 镜像。
服务端提供账号、Matching2/STUN、Bloodborne 召唤 WebAPI；游戏仍由各自电脑上的
bbport 运行，服务端**不是**游戏世界 Host，也不负责转发所有玩家流量。

## 网络设计

| 部署环境 | 对玩家公布的地址 | 说明 |
| --- | --- | --- |
| Windows Docker Desktop 局域网实验 | 当前 Windows WLAN IPv4 | 容器端口映射到宿主机；容器没有独立 LAN IP |
| 真实 Ubuntu Docker Engine | 预留的 `192.168.31.x` | `macvlan` 给容器独立 IP；必须在 DHCP 池外预留并确认未占用 |

TCP `31313` 是登录/游戏协议，UDP `31314` 是匹配/STUN，TCP `31315`
是 WebAPI/召唤 broker。数据库及注册密钥放在 Docker 命名卷 `shadnet-data`，
镜像更新不应清空它。默认传统联机模式，所有客户端须使用相同模式。

shadNet 的 TCP 游戏协议**不加密**。不要做路由器公网端口转发，也不要把这些
端口直接暴露到互联网。跨互联网阶段应让服务器和每位玩家加入同一个加密
tailnet，并测试玩家彼此可达；仅能连到服务器不代表 P2P 召唤可以成功。

## 当前 Windows 电脑上验证

在本目录运行 PowerShell：

旧示例曾使用 `192.168.31.226`，但 WLAN 地址会随网络改变。先用
`ipconfig` 或 `Get-NetIPAddress -AddressFamily IPv4` 核对，再替换以下命令
和启动器地址。当前 v1.7.2 的**本机 Docker 测试栈**使用独立的
`compose.localtest.yaml`，只监听 `127.0.0.1`，不依赖 WLAN 地址。

```powershell
$env:SHADNET_BIND_IP = '<当前 WLAN IPv4>'
docker compose -f compose.desktop.yaml build
docker compose -f compose.desktop.yaml up -d
docker compose -f compose.desktop.yaml ps
Invoke-RestMethod "http://${env:SHADNET_BIND_IP}:31315/status"
```

成功时 `/status` 返回 `{"ok":true,"service":"shadnet-webapi"}`。另一台电脑
也应能打开同一 URL。若不通，先查 Windows 防火墙对 TCP `31313`、UDP
`31314`、TCP `31315` 的入站规则，以及 WLAN 是否为专用网络。不要在未确认
作用范围时开放公用网络配置文件。

首次启动会在命名卷里生成独立的 32 字节随机注册密钥。仅管理员查看：

```powershell
docker exec bbport-shadnet sh -c 'cat /var/lib/shadnet/registration_secret'
```

在启动器的「局域网联机」页中选「原生在线联机（bbport + shadNet）」、
关闭「本机同时作为局域网服务器」，服务器地址填当前 WLAN IPv4，
注册密钥填上面输出，给每位玩家注册不同的联机 ID，再保存设置。
已有 Windows shadNet 数据库不会自动迁入新卷；切换前请备份原数据库。
本机启动器检测到本地地址时可能尝试自动注册，所以注册密钥也要一致。

查看服务日志：

```powershell
docker logs --tail 100 bbport-shadnet
```

停止但保留数据库：

```powershell
docker compose -f compose.desktop.yaml down
```

**不要**加 `-v`；那会删除含有账号数据库和注册密钥的命名卷。

## 将来迁到 Ubuntu 主机并使用独立同网段 IP

`macvlan` 仅受 Linux Docker Engine 支持，Windows Docker Desktop 不支持。
先在路由器 DHCP 池之外预留一个空闲地址，核对 Ubuntu 物理网卡名称和网关。
不要直接使用模板中的 `.240` 示例值。设置以下环境变量后，在 Ubuntu 主机
的本目录执行：

```bash
export SHADNET_IPV4=192.168.31.240   # 替换成实际预留地址
export SHADNET_SUBNET=192.168.31.0/24
export SHADNET_GATEWAY=192.168.31.1  # 以该 Ubuntu 主机实际网关为准
export SHADNET_PARENT_IFACE=eth0      # 以 ip -br link 输出为准
docker compose -f compose.macvlan.yaml up -d --build
```

Linux macvlan 容器默认不能直接和其宿主机通信；如果 Ubuntu 宿主机也要运行
bbport 客户端，需要额外的 host-side macvlan 接口或经另一个网络访问。
Wi-Fi 网卡/接入点也可能拒绝同一物理连接上的多个 MAC，因此优先用有线网卡。
这一步未在当前 Windows Docker Desktop 上做端到端验证。

## 后续在线阶段

1. 先做两台局域网电脑的登录、留言、双向摇铃和 P2P 连接测试，并保留两侧日志。
2. 接入私有 tailnet；服务器和**每位**玩家都要加入，不直接开放公网端口。
3. 核对 bbport 对跨网段地址、STUN 回传地址、P2P 端口及召唤 broker 的处理。
4. 给启动器补充服务器部署方式和连通性诊断，再做容器数据库备份/迁移流程。

这只是服务端基础设施，不等于目前已证明可跨互联网成功联机。
将来试用 tailnet 时，所有玩家（包括开服的电脑）都应加入同一 tailnet；
Windows Docker Desktop 可尝试把 `SHADNET_BIND_IP` 改为宿主机的 tailnet IPv4，
并让全部客户端填同一地址。此路径尚未验证，不能用路由器端口转发来替代。
