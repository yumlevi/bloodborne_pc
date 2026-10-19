# bbport 内置私网桥接（实验）

`bbport-net.exe` 是单独的 userspace `tsnet` 节点，不安装系统级 Tailscale。
`Build.ps1` 用 Go 1.27.1 或本机 Docker 编译 Windows x64 程序；中文启动器把
它装到运行环境的 `net/` 目录。设备身份存放在配置指定的 `state_dir`，**不可
把该目录、设备授权 URL 或任何预授权密钥放进发行包**。

配置文件是 JSON，`--config <绝对路径>` 启动。首次自动入网时，启动器追加
`--auth-key-stdin`，通过标准输入传入短时一次性密钥；密钥不写配置、命令行或
日志。必要字段：`control_url`
（正式环境必须 HTTPS，本机回环地址可用 HTTP）、绝对路径 `state_dir`、
`hostname`。程序在 stdout 输出 JSON 状态事件：`connecting`、`auth`、
`ready`、`traffic`、`error`。`ready` 包含该节点的 `100.64.0.0/10` 地址。
`traffic` 只在游戏 UDP 计数变化后每 15 秒输出累计的成功发送/接收、
目标/来源为玩家 UDP 31316 的子计数、拒收和错误计数，以及最后一个玩家私网
对端 IP:端口；总计数可能含发往服务端 31314 的 STUN，诊断玩家互通应看玩家子计数。
不记录 UDP 内容或入网凭据。启动器将这些
事件保存在运行环境的 `logs/net-traffic-*.log`。对照两台机器同一时段的计数：
若一端发包增长而另一端收包不变，应查私网数据路径；若双向收发都增长但仍未
JoinRoom，应查游戏信令/房间阶段，不能把问题归咎于“没找到召唤”。

客户端由中文启动器自动生成配置。服务端的本机 Docker 测试配置示例（将
`state_dir` 改为自己机器的绝对路径，不与客户端共用）：

```json
{
  "control_url": "http://127.0.0.1:18081",
  "state_dir": "D:\\bbport-private-test\\server-identity",
  "hostname": "bbport-server",
  "tcp_publishes": [
    { "tailnet_port": 31313, "local": "127.0.0.1:31313" },
    { "tailnet_port": 31315, "local": "127.0.0.1:31315" },
    { "tailnet_port": 18080, "local": "127.0.0.1:18080" }
  ],
  "udp_publishes": [
    { "tailnet_port": 31314, "local": "127.0.0.1:31314", "stun_source": true }
  ]
}
```

此配置只监听尾网地址，不替代 Headscale 容器或 shadNet 容器。两台玩家的启动器
仍各自运行独立客户端节点；服务端节点的尾网 IPv4 写入测试包的
`ONLINE_SERVICE.txt` 的 `overlay_server=`，**不要把本机测试节点地址直接
写进公开发行包**。当前 `control=127.0.0.1` 也仅适合同机测试。

客户端 P2P 使用固定物理 UDP `31316`，本地桥接端口 `31416`。桥接内部帧为
`BBTS`、4 字节 IPv4、2 字节网络序端口、原始游戏 UDP 帧；该内部帧只允许
在回环接口传输，尾网侧只传原始游戏帧。需要两个方向都能通；服务器健康与
Headscale 节点 `ready` 并不能证明 Bloodborne 召唤成功。

远端服务器在 `31314/udp`（或明确配置的实验端口 `32314/udp`）的 publish 上
启用 `stun_source`：桥接从已经认证的私网 UDP 来源生成 `BBST` 来源头，shadNet
的远端镜像据此登记玩家的 `100.64.0.0/10:31316` 地址，而不是 Docker 网桥的
`172.18.0.1`。当前无缝部署在独立节点的尾网 `31314/udp` 接收，再转发到实验
容器的本机 `32314/udp`，因此无需扩大生产 ACL。其他 UDP 发布端口仍拒绝
`stun_source`。
客户端的 STUN 目标必须是服务端私网 IP，不能是 TCP 代理 `127.0.0.2`。
远端 bridge 和 shadNet 镜像须一起更新；单独更新其中一个会让 STUN 失败。

本机已验证两个独立节点的 TCP 门户/WebAPI 请求和双向 UDP 人工数据包。
真实双机游戏测试、长期运行、NAT/DERP 切换、访问策略与第三方依赖许可清单
仍需在公开发布前复核。当前 Headscale 测试配置使用公共 DERP 地图，因此它
还不是完全自托管的中继网络。
