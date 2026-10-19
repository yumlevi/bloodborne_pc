# 在线联机本地测试（v1.7.5）

这套服务与现有局域网容器隔离：`compose.localtest.yaml` 使用独立的 Compose
项目、账号数据库和注册密钥，只监听本机 `127.0.0.1`。不会迁移或覆盖旧的
`bbport-shadnet` 容器。游戏仍在玩家电脑上运行；shadNet 负责账号、匹配和召唤
broker，玩家之间还须能建立 P2P 连接。

## 本地单机测试

在 `deploy/shadnet` 目录运行：

```powershell
docker compose -f compose.localtest.yaml up -d --build
docker compose -f compose.localtest.yaml ps
Invoke-RestMethod http://127.0.0.1:18080/v1/status
```

预期返回 `ok: true`、`online_players: 0`、`users: []`。在线人数取自
shadNet 已认证客户端；列表只展示已注册、勾选公开且没有设为隐身的账号。

本地测试栈会自动批准注册申请，正式 Tailscale 栈**不会**自动批准。
可用以下示例走完整申请、查询、注册流程。请自行替换测试 QQ 号和密码，
不要把真实密码或申请 token 贴到公开日志：

```powershell
$application = Invoke-RestMethod http://127.0.0.1:18080/v1/apply -Method Post -ContentType 'application/json' -Body '{"qq":"123456789","nickname":"测试猎人","npid":"testhunter01","show_in_list":true}'
$application.state
$registration = Invoke-RestMethod http://127.0.0.1:18080/v1/register -Method Post -ContentType 'application/json' -Body (@{application_id=$application.application_id;application_token=$application.application_token;registration_key=$application.registration_key;password='ReplaceMe1234!'} | ConvertTo-Json)
$registration.ok
```

`$registration.ok` 为 `True` 才代表 shadNet 账号实际建立。重复使用申请应被拒绝。
管理员统计接口需要本地卷内的 admin key，不能向玩家分发；它提供申请状态、
各账号的累计登录次数及最近 24 小时登录事件数。QQ 原文不入库，只存带服务端
随机密钥的 HMAC 摘要和末四位用于去重、管理。在线状态由服务端定期采样，
频率是估算值，不是游戏时长或每次操作的精确计数。管理员请做好数据库备份和
访问控制。

当前本机测试包的 `packaging/ONLINE_SERVICE.txt` 填的是
`server=127.0.0.1:31313` 与 `portal=http://127.0.0.1:18080`，**只能连接同一台
电脑上的本地 Docker 栈**。另一台电脑用同一包时，`127.0.0.1` 指向它自己，
不会连接这台开服电脑。要做双机测试，应制作单独的**私有测试包**，在
`ONLINE_SERVICE.txt` 的 `control=` 中填自建 Headscale HTTPS 地址，在
`overlay_server=` 中填服务端内置节点的 `100.64.0.0/10` 地址，再重新构建启动器。
客户端可通过网络流量获知服务器地址；“界面隐藏地址”不能防止攻击，安全边界是
私网访问控制、服务端限流以及不开放公网端口。

## 内置 userspace 私网实验

`tools/embedded_net` 使用 `tailscale.com/tsnet`，随中文启动器分发独立的
`net/bbport-net.exe`。它不安装官方 Tailscale 客户端、系统服务或虚拟网卡，也不
切换电脑已有尾网。设备身份仅保存在当前用户的
`%LOCALAPPDATA%\BloodbornePC\embedded-net\identity`；本地 Docker 中先执行
`deploy/headscale/Initialize-AutoEnrollment.ps1` 完成一次性的服务端配置。
新注册的在线账号首次连接时，门户按本机加密保存的申请凭据自动签发 10 分钟、
单次使用的设备密钥，内置节点通过标准输入使用，不再逐台手动批准。
旧版注册时已清除申请凭据的账号不能自动领钥，但已授权私网身份仍可复用；
不应删除旧身份目录或游戏存档。构建发行包前先运行
`tools/embedded_net/Build.ps1`，然后运行根目录 `setup.bat --build-only`。

私网流量包括到服务端的 TCP 31313、31315、18080 和 UDP 31314，以及玩家间
固定 UDP 31316。启动器只在 `overlay_server` 配置有效、内置节点已分配地址时
通过本机 `127.0.0.2` 代理游戏与门户流量；未启用时保留旧路径。服务端可以
在受控主机上用同一个 `bbport-net` 可执行文件配置 `tcp_publishes` 和
`udp_publishes`，将 tailnet 端口转给本机 Docker 映射端口。不要把同一份
`state_dir` 同时给两个节点，也不要把身份目录或授权链接打进玩家包。

本机已验证：Go 单元测试、Windows 编译、账号注册到 Headscale 自动入网；
两个独立节点间的 TCP 门户/WebAPI 请求，以及双向 UDP 桥接的人工数据包。
仍须在两台真实玩家机器上验证游戏内双向匹配、召唤和持续的数据传输。因此
这一入口目前标为实验性，不能把节点显示“已连接”当作游戏联机成功。

## Tailscale 测试栈

`compose.tailscale.yaml` 让 Tailscale、shadNet 和门户共享一个网络命名空间；
不映射宿主机端口，也不做路由器公网转发。服务器、两位测试玩家必须先加入
**同一个受控 tailnet**。在本机以环境变量设置 Tailscale auth key 后再启动
（不要写入仓库或聊天），并在管理控制台限制设备、访问规则与密钥有效期：

```powershell
$env:TS_AUTHKEY = '<your-private-test-auth-key>'
docker compose -f compose.tailscale.yaml up -d --build
docker compose -f compose.tailscale.yaml ps
Remove-Item Env:TS_AUTHKEY
```

从容器内查看 tailnet 地址：

```powershell
docker compose -f compose.tailscale.yaml exec tailscale tailscale ip -4
```

正式栈的申请默认 `pending`。管理员应通过受保护的管理端点审核，当前只提供
API，没有管理界面。管理员可以在可信本机读取门户卷中的 `admin_key`，
以 `Authorization: Bearer <admin_key>` 调用 `GET /v1/admin/usage` 查看
`application_id`，再对 `POST /v1/admin/approve/<application_id>` 作出审核。
不要把 admin key 发给玩家，或记录到聊天/公开日志。私有测试前先完成两端的
Tailscale 可达性、账号登录和
双向召唤验证。没有 auth key、客户端登录和尾网 ACL 配置时，不能认为
“Tailscale 已启用”或在线联机已经可用。

## 安全和待验证项

- shadNet 的 TCP 游戏协议本身不加密；绝不能直接把 31313/31314/31315
  或门户 18080 映射到公网。
- 玩家详情仅在勾选同意后显示 NPID、昵称和当前游戏；QQ 及 IP 不在公共接口
  中返回。管理员接口仍属敏感数据，不应公开 admin key。
- `ONLINE_SERVICE.txt` 只放地址，**绝不放 shadNet 全局注册密钥、Tailscale
  auth key 或管理员 key**。
- 当前已完成本机 Docker 状态、申请、注册、管理员统计接口测试；跨两台真实玩家电脑的尾网登录、
  召唤和 P2P 回归测试仍需进行。旧局域网联机正常不等于跨互联网联机正常。
