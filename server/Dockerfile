FROM --platform=$BUILDPLATFORM mcr.microsoft.com/dotnet/sdk:10.0-alpine AS build
WORKDIR /source
ARG TARGETARCH
RUN apk add --no-cache clang gcc musl-dev zlib-dev
COPY server/src/ ./
RUN case "$TARGETARCH" in amd64) rid=linux-musl-x64 ;; arm64) rid=linux-musl-arm64 ;; *) exit 1 ;; esac \
    && dotnet publish LanPlayServer.csproj -c Release -r "$rid" -o /published -p:StripSymbols=true

FROM alpine:3.22
RUN apk add --no-cache icu-libs libstdc++ zlib \
    && addgroup -g 10001 -S ldn && adduser -u 10001 -S -G ldn ldn \
    && mkdir -p /app /data/potluck /usr/share/licenses/ldn-server \
    && touch /data/potluck/bannedips.txt && chown -R 10001:10001 /data
COPY --from=build /published/LanPlayServer /app/LanPlayServer
COPY --from=build /published/gamelist.json /app/gamelist.json
COPY server/src/LICENSE.txt /usr/share/licenses/ldn-server/LICENSE.txt
# The status page, served only when LDN_WEB_ENABLED is true
COPY web/ /app/web/
RUN rm -f /app/web/DESIGN.md /app/web/og-image.svg
WORKDIR /app
ENV DOTNET_SYSTEM_GLOBALIZATION_INVARIANT=false \
    IP_BAN_FILE_PATH=/data/potluck/bannedips.txt \
    LDN_WEB_ROOT=/app/web
USER 10001:10001
EXPOSE 30456/tcp
ENTRYPOINT ["/app/LanPlayServer"]
