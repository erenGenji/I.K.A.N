# IKAN Project

Embedded IoT with **NO WIFI** needed. Equipped with Embedded Kendryte-based NPU for AI. **No expensive SOC** (Raspberry Pi, Jetson, mini computer). Built for fish farmers.

## Operating Modes

| Stand-Alone Mode  | Feeder Mode |
| --- | --- |
| AI-based water condition monitoring | AI-assisted automated feeding |

## AI Models used for Stand-Alone Mode


stateDiagram-v2
    Bubble_Detection_Model --> Algae_Bloom_Detection_Model
    Algae_Bloom_Detection_Model --> Bubble_Detection_Model

stateDiagram-v2
    FishSize_model --> FishFeed_Tracking_model
    FishFeed_Tracking_model --> FishMortality_model
    FishMortality_model --> FishFeed_Tracking_model
    FishMortality_model --> FishSize_model
