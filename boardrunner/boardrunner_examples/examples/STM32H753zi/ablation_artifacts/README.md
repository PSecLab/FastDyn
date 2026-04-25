# STM32H753ZI Ablation Artifacts

Artifacts supporting the BoardRunner component-ablation results on NUCLEO-H753ZI.

## Workloads

| Peripheral | Firmware | Goal |
|---|---|---|
| USART | `UART_ComIT.elf` (interrupt-driven echo) | TX banner, RX/TX echo loop |
| SPI | `SPI_Firmware.elf` (full-duplex polling) | Read BME280 chip ID (`0xD0` → `0x60`) |
| ADC + DMA | `ADC_DMA_Transfer.elf` (continuous, dual-mode) | Compositional: DMA1+DMAMUX1 model + ADC1+ADC2+ADC12_Common model, joint convergence |

## Variants

| Variant | Disabled component | Flag |
|---|---|---|
| `full_medium` | (none) | — |
| `ablation_no_encoder` | Encoder | `--no-encoder` on `fastdyn generate` and every `fastdyn verifier` |
| `ablation_no_rca` | RCA | `--no-rca` on every `fastdyn verifier` |
| `ablation_no_vio` | VIO API layer | `--no-vio` on `fastdyn generate` and every `fastdyn verifier` |
| `ablation_no_verifier` | Verifier feedback loop (single-shot) | n/a — cell is the iter-1 LLM call from `full_medium/` |

## Setup

- LLM: GPT-5.4, `--reasoning-effort medium`, `--evaluate` for per-call metrics
- Iteration cap: 10 LLM calls per cell
- 1 sample per cell

## Layout

```
ablation_artifacts/
├── README.md
├── USART/         README.md + 4 variant dirs
├── SPI/           README.md + 4 variant dirs (+ used_failure_model.c for A2)
└── ADC_with_DMA/  README.md + 5 variant dirs (+ adc/dma_used_failure_model.c for A2)
```

Each variant directory holds the saved LLM I/O for that cell:
- `NNN_prompt.txt`, `NNN_response_1.txt` — every LLM call
- `metrics.jsonl` — one JSON object per call (tokens, latency, type)
- `generated_model.c` — final synthesized model (`adc_*` / `dma_*` prefixed for ADC+DMA)

## Metrics format

```json
{"prompt_tokens": 5434, "completion_tokens": 6316, "reasoning_tokens": 4057,
 "total_tokens": 11750, "latency_seconds": 101.8, "iteration": 1, "type": "initial",
 "result": "success"}
```

`type` is `initial` for first generation, `revision` for SEARCH/REPLACE corrections.
