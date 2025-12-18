In a separate terminal, run:
```bash
    socat -d -d PTY,link=/tmp/usart1_pty,raw,echo=0 STDIO
```