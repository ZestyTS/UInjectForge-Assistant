FROM wiiuenv/devkitppc:20220907

WORKDIR /app
CMD ["make", "-j2"]
