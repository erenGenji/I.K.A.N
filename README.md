# IKAN Project

Embedded IOT with **NO WIFI** needed. Equipped with Embedded Kendryte based NPU for AI. **No expensive SOC** (Raspberry pi, jetson, mini computer) built for Fish Farmer


| Stand-Alone Mode  | Feeder Mode |
| --- | --- |
|  |  |


## AI Models used for Stand-Alone Mode

```mermaid
stateDiagram-v2 

Bubble_Detection_Model --&gt; Algae_Bloom_Detection_Model
Algae_Bloom_Detection_Model --&gt; Bubble_Detection_Model

```

## AI Models used for Feeder Mode 

```mermaid
stateDiagram-v2 

FishSize_model --&gt; FishFeed_Tracking_model
FishFeed_Tracking_model --&gt; FishMortality_model
FishMortality_model --&gt; FishFeed_Tracking_model
FishMortality_model --&gt; FishSize_model
```
