# Headscale 私网控制服务：本机试验

本目录与 `deploy/shadnet/compose.localtest.yaml` 共用私有 Docker 控制网络，
但数据卷与公开端口仍分开。Headscale 管设备入网；
shadNet 仍管 Bloodborne 账号、留言和召唤。这里的控制地址只映射到
`127.0.0.1:18081`，不能让另一台电脑或互联网玩家连接。也不含管理员 API key、
预授权密钥或 Tailscale 的设备密钥。

在本目录运行：

```powershell
docker compose -f compose.localtest.yaml up -d
docker compose -f compose.localtest.yaml ps
Invoke-RestMethod http://127.0.0.1:18081/health
docker compose -f compose.localtest.yaml exec headscale headscale users create localtest
docker compose -f compose.localtest.yaml exec headscale headscale users list
```

启动器随包提供独立的 userspace 私网节点，无需安装 Windows 官方 Tailscale
客户端。现在已注册的在线账号首次连接时，会从门户自动申请 10 分钟有效、
只能使用一次的 Headscale 入网密钥，不再逐台运行 `auth register`。
请先确认 `users list` 中供本地测试使用的用户 ID（通常是 1），再在仓库根目录运行：

```powershell
.\deploy\headscale\Initialize-AutoEnrollment.ps1 -UserId 1
```

脚本只在本地 Docker 首次创建一个 30 天有效的 Headscale 管理 API key，写入
门户的私有 Docker 卷；再次运行默认不会重复签发。到期前用 `-Rotate` 轮换。
门户仅给已完成 shadNet 在线账号注册、并持有原申请凭据的客户端签发入网密钥，
每账号每 24 小时最多 5 次。凭据只通过启动器与内置节点的标准输入传递；
不会进入发行包、`client.json` 或命令行参数。已入网节点使用本地持久身份，
重启启动器不重复领钥。旧版启动器注册后曾清掉申请凭据，旧账号目前不能自动
迁移，切勿为此删除现有游戏存档或私网身份；可暂用已有已授权设备，后续需加
安全的账号迁移流程。

本地门户 `PORTAL_AUTO_APPROVE=true` 只为测试方便：QQ 与昵称未做所有权验证，
不等于真实身份认证。公开服务不能仅凭 QQ/昵称自动放行；正式部署应关闭该
选项，并增加真正的账号验证、设备吊销、访问策略与审计。当前 Headscale
0.29.4 管理 API key 为全权限，必须只保存在服务端，门户卷和容器均需保护。

当前 `derp.urls` 仍是 Tailscale 提供的默认公共 DERP 地图，用于最初的连接性测试，
因此**还不是完全自托管的中继链路**。正式环境必须启用 Headscale 内置 DERP、
设置真实公网地址、开放 UDP 3478，并在双机验证后再考虑移除默认 DERP 地图；
否则无法直连的玩家可能完全失去中继。Headscale 控制服务和 shadNet 游戏服务
是不同组件：成功加入私网不等于游戏账号注册成功，更不等于召唤已经通畅。

本机包内的 `ONLINE_SERVICE.txt` 仍把 shadNet 与账号门户映射到 `127.0.0.1`。
异地测试之前，需要为自建控制端配置玩家可达的 HTTPS 地址，并让服务端的
内置节点发布 shadNet 端口，在包中配置 `overlay_server=` 为该节点的私网 IP。
只把 Headscale 放到公网，不会自动让游戏服务可用。

**正式部署尚未进行。** Headscale 官方建议用具有公网可达性的 HTTPS 443
控制地址；只有 Headscale 本机健康不代表 Windows 客户端已经登录，也不代表
玩家之间的游戏 UDP 可达。对外部署需要域名、有效 TLS、可信账号验证与设备签发策略、访问策略，
并视 NAT 情况配置 DERP/STUN。Headscale Docker 镜像由项目提供，但容器部署
文档标注为社区维护；更新镜像前要备份命名卷，并按官方版本升级说明逐级迁移。

参考：
- https://headscale.net/stable/setup/requirements/
- https://headscale.net/stable/setup/install/container/
- https://headscale.net/stable/usage/connect/windows/
- https://headscale.net/stable/ref/registration/
- https://headscale.net/stable/ref/derp/
