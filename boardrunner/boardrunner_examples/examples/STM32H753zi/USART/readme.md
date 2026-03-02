This firmware waits for you to press push button and then transmits data and after transmission, it will wait for you to type data and will show on the terminal.

Use this command when running the model to interact with the pty.
```bash
socat -d -d PTY,link=/tmp/usart1_pty,raw,echo=0 STDIO
```