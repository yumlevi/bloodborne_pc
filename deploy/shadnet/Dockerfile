FROM ubuntu:24.04 AS build

ARG SHADNET_COMMIT=5a849a192fd4450593c57b10697316ac324c856e
ARG SHADNET_BUILD_JOBS=2
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    git ca-certificates cmake ninja-build build-essential \
    qt6-base-dev qt6-httpserver-dev libqt6sql6-sqlite \
    && rm -rf /var/lib/apt/lists/*
RUN apt-get update && apt-get install -y --no-install-recommends \
    qt6-websockets-dev && rm -rf /var/lib/apt/lists/*
RUN apt-get update && apt-get install -y --no-install-recommends curl \
    && rm -rf /var/lib/apt/lists/*
# Use immutable GitHub archives: a recursive Git clone is unreliable on hosts
# with intermittent TLS resets. The protobuf archive is the exact gitlink in
# SHADNET_COMMIT, not a floating dependency.
RUN curl --fail --location --retry 5 --retry-all-errors \
      "https://codeload.github.com/Wozzardman/shadnet-p2p/tar.gz/$SHADNET_COMMIT" \
      --output /tmp/shadnet.tar.gz \
    && curl --fail --location --retry 5 --retry-all-errors \
      "https://codeload.github.com/shadexternals/protobuf/tar.gz/refs/heads/main" \
      --output /tmp/protobuf.tar.gz
RUN echo "7a24d220f0b4d533bb5ba79fc44eff680358785829568ee954549ab9edce2ed4  /tmp/shadnet.tar.gz" | sha256sum -c - \
    && echo "dd1235231a83957f5471ceae41078590315931dc2be6d6914da006387e6fac50  /tmp/protobuf.tar.gz" | sha256sum -c - \
    && mkdir -p /src \
    && tar -xzf /tmp/shadnet.tar.gz -C /src --strip-components=1 \
    && mkdir -p /src/externals/protobuf \
    && tar -xzf /tmp/protobuf.tar.gz -C /src/externals/protobuf --strip-components=1 \
    && rm /tmp/shadnet.tar.gz /tmp/protobuf.tar.gz
# Protobuf 36 falls back to FetchContent for Abseil. Supplying a checked
# source archive avoids an unreliable git clone during CMake configure.
RUN curl --fail --location --retry 5 --retry-all-errors \
      "https://codeload.github.com/abseil/abseil-cpp/tar.gz/refs/tags/20250512.1" \
      --output /tmp/abseil.tar.gz \
    && echo "9b7a064305e9fd94d124ffa6cc358592eb42b5da588fb4e07d09254aa40086db  /tmp/abseil.tar.gz" | sha256sum -c - \
    && mkdir -p /src/externals/abseil \
    && tar -xzf /tmp/abseil.tar.gz -C /src/externals/abseil --strip-components=1 \
    && rm /tmp/abseil.tar.gz
COPY patches/qt64-server-api.patch /tmp/qt64-server-api.patch
RUN cd /src && git apply --unidiff-zero /tmp/qt64-server-api.patch
COPY patches/online-stats.patch /tmp/online-stats.patch
RUN cd /src && git apply --unidiff-zero /tmp/online-stats.patch
# Keep a bounded claim lease and expose cleanup/claim diagnostics only when
# SHADNET_SEAMLESS is enabled. The traditional endpoint can opt in separately.
COPY patches/seamless-rejoin.patch /tmp/seamless-rejoin.patch
RUN cd /src && git apply --check /tmp/seamless-rejoin.patch \
    && git apply /tmp/seamless-rejoin.patch
# Bound stale claims even when the guest stops advertising after the first claim.
COPY patches/seamless-stale-claim.patch /tmp/seamless-stale-claim.patch
RUN cd /src && git apply --unidiff-zero --check /tmp/seamless-stale-claim.patch \
    && git apply --unidiff-zero /tmp/seamless-stale-claim.patch
# In seamless mode, verify TargetUserId before trusting the request's SessionId.
COPY patches/seamless-claim-target.patch /tmp/seamless-claim-target.patch
RUN cd /src && git apply --unidiff-zero --check /tmp/seamless-claim-target.patch \
    && git apply --unidiff-zero /tmp/seamless-claim-target.patch
# This pinned upstream version needs to retain the authenticated tailnet
# source when Docker's loopback UDP port mapping rewrites the sender address.
COPY overrides/stun_server.cpp /src/src/stun_server.cpp
COPY overrides/stun_server.h /src/src/stun_server.h
RUN --mount=type=cache,target=/src/build cmake -S /src -B /src/build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
    -DFETCHCONTENT_SOURCE_DIR_ABSL=/src/externals/abseil \
    && cmake --build /src/build --target test_bloodborne_summon_broker \
       --parallel "$SHADNET_BUILD_JOBS" \
    && ctest --test-dir /src/build --output-on-failure \
       -R '^bloodborne_summon_broker$' \
    && cmake --build /src/build --parallel "$SHADNET_BUILD_JOBS" -- -k 0 \
    && mkdir -p /src/artifacts \
    && cp /src/build/shadnet /src/build/worlds.cfg \
       /src/build/scoreboards.cfg /src/artifacts/

FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates curl libqt6core6t64 libqt6network6t64 \
    libqt6sql6t64 libqt6sql6-sqlite libqt6httpserver6 \
    libqt6concurrent6 \
    && rm -rf /var/lib/apt/lists/*
RUN groupadd --gid 10001 shadnet && useradd --uid 10001 --gid 10001 \
    --no-create-home --shell /usr/sbin/nologin shadnet \
    && mkdir -p /opt/shadnet /var/lib/shadnet/db \
    && chown -R shadnet:shadnet /var/lib/shadnet
COPY --from=build /src/artifacts/ /opt/shadnet/
COPY --from=build /src/LICENSES/GPL-2.0-or-later.txt /usr/share/doc/shadnet/LICENSE
COPY entrypoint.sh /usr/local/bin/shadnet-entrypoint
RUN chmod 755 /usr/local/bin/shadnet-entrypoint
USER shadnet
WORKDIR /var/lib/shadnet
VOLUME ["/var/lib/shadnet"]
EXPOSE 31313/tcp 31314/udp 31315/tcp
HEALTHCHECK --interval=30s --timeout=5s --start-period=15s --retries=3 \
    CMD curl --fail --silent http://127.0.0.1:31315/status >/dev/null || exit 1
ENTRYPOINT ["/usr/local/bin/shadnet-entrypoint"]
