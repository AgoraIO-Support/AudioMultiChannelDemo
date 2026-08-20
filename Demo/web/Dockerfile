# Web 多声道 demo 镜像：node 构建 + nginx 托管
#   docker build -t agora-multichannel-web .
#   docker run --rm -p 8080:80 agora-multichannel-web

FROM node:20-alpine AS build
WORKDIR /app
COPY package.json package-lock.json ./
RUN npm ci
COPY . .
# VITE_BASE：子路径部署时传入，如 docker build --build-arg VITE_BASE=/multiopus/ .
ARG VITE_BASE=/
ENV VITE_BASE=$VITE_BASE
# VITE_BUILD_ID：显式指定构建 ID；未指定时 vite.config.ts 会生成时间戳
ARG VITE_BUILD_ID
ENV VITE_BUILD_ID=$VITE_BUILD_ID
RUN npm run build

FROM nginx:stable-alpine
COPY --from=build /app/dist /usr/share/nginx/html
COPY nginx.conf /etc/nginx/conf.d/default.conf
EXPOSE 80
