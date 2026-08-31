# AWS 测试服务器部署手册 — 音频多声道 Demo

> 记录 2026-08-18 将 `Demo/linux` 与 `Demo/web` 部署到 AWS 测试服务器的完整步骤与细节，供后续复用/更新部署时参考。

## 1. 服务器与域名概况

| 项目 | 值 | 备注 |
| --- | --- | --- |
| 实例 | i-09ed5c34bcab054d3（Xiaoyingtao_SIP） | Ubuntu，x86_64，2 vCPU / 1.9 GB 内存 |
| 公网 IP | 54.152.163.4 | 实例重启后可能变化，见 `Resource/aws_test_server.txt` |
| SSH | `ssh -i Resource/Xiaoyingtao_SIP_key_pair.pem ubuntu@54.152.163.4` | 安全组 22 端口仅放行指定源 IP |
| 域名 | xiaoyingtao.convoai.club | Cloudflare → 服务器 Nginx(443) → 各 Docker 容器 |
| 服务器项目目录 | `/home/ubuntu/multiopus/` | 本次部署新建 |
| 已有服务（勿动） | `xiaoyingtao-sip-gateway`(8088)、`asterisk`、`bridge`、`bridge-cb` | 见 `Resource/aws_test_server.txt` |

**安全组注意**：SSH（22）只对指定公网 IP 放行。本机宽带 IP 变化后 SSH 会连不上，
需先到 AWS 控制台把该规则的 Source 改为新 IP（当前实测出口 IP 为 114.94.148.34）。

**访问链路**：

```
浏览器 → Cloudflare → 服务器 Nginx(443, Let's Encrypt 证书)
   ├── /                → 127.0.0.1:8088  xiaoyingtao-sip-gateway（已有，勿动）
   └── /multiopus/      → 127.0.0.1:8090  multiopus-web（本次新增）
```

选择子路径（`/multiopus/`）而非子域名的原因：子域名需要在 Cloudflare 改 DNS，
子路径只需改服务器上的 nginx 配置，且不占用新端口、复用现有证书。

## 2. 本次部署产物一览

| 产物 | 位置 | 说明 |
| --- | --- | --- |
| Web demo 访问地址 | https://xiaoyingtao.convoai.club/multiopus/ | 静态站点，经 nginx 反代 |
| 文档站访问地址 | https://xiaoyingtao.convoai.club/multiopus/docs/ | `dist/docs/` 中的静态文件，经同一 Web 容器与 nginx 反代服务 |
| `multiopus-web` 容器 | 127.0.0.1:8090 → 80 | nginx:stable-alpine，挂载 dist，`--restart unless-stopped` |
| `agora-multichannel-linux:latest` 镜像 | 服务器 docker | 含 5 个 sample 二进制 + 素材 + SDK 动态库 |
| 服务器端文件 | `~/multiopus/{Demo/linux, LinuxSDKFolder/X86_64, web/dist, web/nginx.conf, .dockerignore}` | 构建上下文与站点文件 |
| nginx 备份 | `~/xiaoyingtao.convoai.club.bak-20260818` | 改动前的站点配置备份 |

## 3. 部署步骤

### 3.1 前提：Web demo 支持子路径 base

子路径部署必须让前端资源带 `/multiopus/` 前缀，涉及两处改动（已合入仓库）：

- `Demo/web/vite.config.ts`：`base: process.env.VITE_BASE || '/'`
- `Demo/web/src/Multiopus.vue`：WAV 素材 URL 从绝对路径 `/8_Channel_ID.wav` 改为
  `` `${import.meta.env.BASE_URL}8_Channel_ID.wav` ``

`Demo/web/Dockerfile` 也支持 `--build-arg VITE_BASE=/multiopus/`（注意：该 Dockerfile 需以
`Demo/` 为构建上下文，如 `cd Demo && docker build -f web/Dockerfile ...`，构建时 `docs/` 会被复制进 `dist/docs`）。

### 3.2 本地构建 Web 产物

服务器内存仅 1.9 GB，不适合跑 npm 构建，因此**在本地构建后只上传 dist**：

```bash
BUILD_ID=$(git rev-parse --short HEAD)
BUILD_ID="$BUILD_ID" node Demo/docs/build.mjs
cd Demo/web
VITE_BASE=/multiopus/ VITE_BUILD_ID="$BUILD_ID" npm run build
# 验证 dist/index.html 中资源路径为 /multiopus/assets/...
grep -o 'src="[^"]*"' dist/index.html
# 文档与其资源已随 Vite 构建复制到 dist/docs/
test -f dist/docs/index.html
```

### 3.3 上传文件到服务器

```bash
KEY=Resource/Xiaoyingtao_SIP_key_pair.pem
SRV=ubuntu@54.152.163.4

ssh -i $KEY $SRV 'mkdir -p ~/multiopus/Demo ~/multiopus/LinuxSDKFolder ~/multiopus/web'

# Linux demo 源码（排除本地构建产物）
rsync -az -e "ssh -i $KEY" --exclude='/build/' --exclude='/out/' \
    Demo/linux/ $SRV:~/multiopus/Demo/linux/

# x86_64 SDK（排除 tgz 压缩包，约 50 MB）
rsync -az -e "ssh -i $KEY" --exclude='*.tgz' \
    LinuxSDKFolder/X86_64/ $SRV:~/multiopus/LinuxSDKFolder/X86_64/

# Web 产物与容器 nginx 配置
rsync -az -e "ssh -i $KEY" Demo/web/dist/ $SRV:~/multiopus/web/dist/
scp -i $KEY Demo/web/nginx.conf $SRV:~/multiopus/web/nginx.conf

# Docker 构建上下文 ignore（放 ~/multiopus/ 根下）
scp -i $KEY .dockerignore $SRV:~/multiopus/.dockerignore
```

### 3.4 服务器上构建 Linux demo 镜像

```bash
ssh -i $KEY $SRV
cd ~/multiopus
docker build -f Demo/linux/Dockerfile -t agora-multichannel-linux .

# 验证：5 个二进制 + 素材存在，动态库依赖可解析
docker run --rm agora-multichannel-linux ls /app /app/public
docker run --rm agora-multichannel-linux ldd /app/sample_send_opus8 | grep "not found" || echo OK
```

### 3.5 启动 Web 容器

```bash
docker run -d --name multiopus-web --restart unless-stopped \
  -p 127.0.0.1:8090:80 \
  -v /home/ubuntu/multiopus/web/dist:/usr/share/nginx/html:ro \
  -v /home/ubuntu/multiopus/web/nginx.conf:/etc/nginx/conf.d/default.conf:ro \
  nginx:stable-alpine

# 本机验证
curl -s -o /dev/null -w '%{http_code}\n' http://127.0.0.1:8090/
curl -s -o /dev/null -w '%{http_code}\n' http://127.0.0.1:8090/8_Channel_ID.wav
curl -s -o /dev/null -w '%{http_code}\n' http://127.0.0.1:8090/docs/
```

### 3.6 服务器 nginx 增加 /multiopus/ 反代

配置文件：`/etc/nginx/sites-enabled/xiaoyingtao.convoai.club`。
在 443 server 块内、`location / {` **之前**插入：

```nginx
    # 多声道 demo（2026-08-18）：Agora 音频多声道 Web 验证页，
    # 反代到本机 multiopus-web 容器（127.0.0.1:8090，静态站点）
    location = /multiopus {
        return 301 /multiopus/;
    }

    location /multiopus/ {
        proxy_pass http://127.0.0.1:8090/;
        proxy_http_version 1.1;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;
    }
```

要点：`proxy_pass http://127.0.0.1:8090/;` 末尾的 `/` 会剥掉 `/multiopus/` 前缀，
容器内站点按根路径服务。

安全改动流程（改线上配置务必遵守）：

```bash
CONF=/etc/nginx/sites-enabled/xiaoyingtao.convoai.club
# 1. 备份——绝不能放在 sites-enabled/ 目录内（见第 5 节坑 1）
sudo cp "$CONF" ~/xiaoyingtao.convoai.club.bak-$(date +%Y%m%d)
# 2. 写入新配置后测试，失败则恢复
sudo nginx -t && sudo systemctl reload nginx
# 失败时：sudo cp 备份 "$CONF" && sudo nginx -t
```

### 3.7 端到端验证

```bash
# 本机执行
curl -s -o /dev/null -w '%{http_code}\n' https://xiaoyingtao.convoai.club/multiopus/        # 200
curl -s -o /dev/null -w '%{http_code}\n' https://xiaoyingtao.convoai.club/multiopus/8_Channel_ID.wav  # 200
curl -s -o /dev/null -w '%{http_code}\n' https://xiaoyingtao.convoai.club/multiopus/docs/  # 200
curl -s https://xiaoyingtao.convoai.club/health                                            # 原服务不受影响
```

再用浏览器打开 https://xiaoyingtao.convoai.club/multiopus/ 确认页面完整渲染。

## 4. 日常使用与更新

### 运行 Linux sample（服务器上）

```bash
# RTC 媒体走 UDP，必须 --network host
docker run --rm -it --network host agora-multichannel-linux \
    ./sample_send_wav_multiopus --channelId <name> --token <token> \
    --audioFile "public/8_Channel_ID_signed_16_bit_pcm.wav" \
    --numOfChannels 8 --custom_channel_num 8 --custom_payload_type 125 --sampleRate 48000
```

### 更新 Web demo

```bash
# 本地重新生成文档 Build ID 并构建（docs 会复制到 dist/docs/）
BUILD_ID=$(git rev-parse --short HEAD)
BUILD_ID="$BUILD_ID" node Demo/docs/build.mjs
cd Demo/web && VITE_BASE=/multiopus/ VITE_BUILD_ID="$BUILD_ID" npm run build
# 上传并重启容器（dist 是挂载的，替换文件即可）
rsync -az --delete -e "ssh -i $KEY" dist/ $SRV:~/multiopus/web/dist/
# 静态文件无需重启容器；如改了 nginx.conf 则：docker restart multiopus-web
```

### 更新文档站（Demo/docs）

文档已包含在 `Demo/web/dist/docs/`。按“更新 Web demo”的命令生成 Build ID、重新构建并同步整个
`dist/`；不再配置宿主机 nginx alias，也不再单独上传 `Demo/docs/`。

### 更新 Linux demo

```bash
rsync -az -e "ssh -i $KEY" --exclude='/build/' --exclude='/out/' Demo/linux/ $SRV:~/multiopus/Demo/linux/
ssh -i $KEY $SRV 'cd ~/multiopus && docker build -f Demo/linux/Dockerfile -t agora-multichannel-linux .'
```

### 回滚 nginx

```bash
ssh -i $KEY $SRV
sudo cp ~/xiaoyingtao.convoai.club.bak-20260818 /etc/nginx/sites-enabled/xiaoyingtao.convoai.club
sudo nginx -t && sudo systemctl reload nginx
docker rm -f multiopus-web   # 如需一并下线
```

## 5. 踩过的坑（重要）

1. **nginx 备份文件不能放在 `sites-enabled/` 内**。nginx 会加载该目录下所有文件
   （不看扩展名），`.bak` 文件会被当作站点配置，造成 `duplicate listen options` 报错
   导致 `nginx -t` 失败。备份一律放 `~/` 或 `/etc/nginx/` 其他位置。
2. **SSH 源 IP 白名单**。安全组 22 端口只放行单个 /32 IP，本机宽带 IP 变化后先改安全组。
3. **子路径部署两个前缀缺一不可**：vite 构建 base（影响 JS/CSS 资源 URL）和代码内
   素材 fetch 的 `import.meta.env.BASE_URL` 前缀（影响 public/ 下的 WAV）。只改前者会
   出现页面能开但 WAV 404。
4. **服务器内存 1.9 GB**，不要在服务器上跑 `npm ci`/vite build；Web 产物一律本地构建后上传。
5. **Linux demo 容器必须 `--network host`**，否则 RTC UDP 媒体可能不通。

## 6. 相关文件索引

| 文件 | 作用 |
| --- | --- |
| `Resource/aws_test_server.txt` | 服务器实例/安全组/已有服务详情（含 SSH 私钥文件名） |
| `Resource/Xiaoyingtao_SIP_key_pair.pem` | SSH 私钥（权限 400，勿提交泄露） |
| `Demo/linux/Dockerfile` | Linux demo 镜像（构建上下文需含 `LinuxSDKFolder`，故从仓库根构建） |
| `Demo/web/Dockerfile` | Web demo 镜像（支持 `--build-arg VITE_BASE=`） |
| `Demo/web/nginx.conf` | Web 容器内站点配置（SPA 回退 + gzip） |
| `.dockerignore`（仓库根） | 减小 Linux 镜像构建上下文 |
