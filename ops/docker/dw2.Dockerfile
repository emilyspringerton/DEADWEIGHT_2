# dw2_server + dw2_bot image: hermetic build from tracked source, no external deps (neither needs SDL2 -- only
# apps/local and apps/client do). The bot pool runs as a second container in the same pod (EMILY/gitops/specs/deadweight2.pod). Matches SHANKPIT's own ops/docker/*.Dockerfile
# convention (build from source in Cloud Build, not DEADWEIGHT's own binary-staging stopgap).
FROM gcc:13-bookworm AS build
WORKDIR /src
COPY . .
RUN gcc -std=c99 -Wall -Wextra -Werror -Icore -Iapps/server -DPARENA_NO_GRAPHICS -DDW2_VERSION='"0.0.0-dev"' -O2 \
    core/items.c core/combat.c core/round.c core/protocol.c core/http.c core/iduna.c \
    core/cannon.c core/parena_runtime.c cannon/cannon_decision_gen.c \
    apps/server/main.c \
    -o /src/dw2_server -lpthread -lm
RUN gcc -std=c99 -Wall -Wextra -Werror -Icore -O2 apps/bot/main.c core/protocol.c -o /src/dw2_bot -lm

FROM debian:12-slim
RUN apt-get update && apt-get install -y --no-install-recommends ca-certificates && rm -rf /var/lib/apt/lists/*
WORKDIR /app
COPY --from=build /src/dw2_server /app/dw2_server
COPY --from=build /src/dw2_bot /app/dw2_bot
RUN chmod 755 /app/dw2_server /app/dw2_bot
ENTRYPOINT ["/app/dw2_server"]
