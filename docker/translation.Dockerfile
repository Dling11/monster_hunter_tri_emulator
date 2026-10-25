FROM mcr.microsoft.com/dotnet/sdk:8.0-noble@sha256:ada801965b46b26af97d6d366300ec06ca793c581259664c226253d9da55d254

ENV DOTNET_CLI_TELEMETRY_OPTOUT=1 \
    DOTNET_NOLOGO=1 \
    DOTNET_SKIP_FIRST_TIME_EXPERIENCE=1 \
    DOTNET_BIN=/usr/share/dotnet/dotnet \
    WIICOMPILED_ROOT=/opt/mh3-toolchain \
    PYTHONDONTWRITEBYTECODE=1

RUN apt-get update \
    && apt-get install -y --no-install-recommends python3 curl ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Pinned upstream executable; no disc or game-derived files enter the image.
ARG NOD_VERSION=v2.0.0-alpha.12
ARG NOD_SHA256=ee8345c004248eb82a86dd0c6068e0fcf3dc75baf68d18fe949bf4589822f5a5
RUN curl --fail --location --retry 3 \
      "https://github.com/encounter/nod/releases/download/${NOD_VERSION}/nodtool-linux-x86_64" \
      --output /usr/local/bin/nodtool \
    && echo "${NOD_SHA256}  /usr/local/bin/nodtool" | sha256sum --check \
    && chmod 755 /usr/local/bin/nodtool

# Build away from the checkout; bin/obj output stays outside the source snapshot.
COPY toolchain/ /opt/mh3-toolchain/
RUN dotnet build /opt/mh3-toolchain/translator/src/Translator.Cli/Translator.Cli.csproj \
      --configuration Release --nologo --verbosity quiet

COPY scripts/dev-container.py /opt/mh3-dev.py
WORKDIR /workspace
ENTRYPOINT ["python3", "/opt/mh3-dev.py"]

