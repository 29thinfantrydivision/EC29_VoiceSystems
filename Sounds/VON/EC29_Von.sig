AudioSignalResClass {
 Inputs {
  IOPItemInputClass {
   id 1
   name "TransmissionQuality"
   tl -352 176
   children {
    6 7 14
   }
   value 1
  }
  IOPInputVariableClass {
   id 20
   name "EC29_JamStrength"
   tl -352 800
   children {
    21 22 23 24
   }
   varName "EC29_JamStrength"
   varResource "{3DA1A848EE00C426}Sounds/VON/RadioEarRouting.conf"
  }
  IOPInputValueClass {
   id 9
   name "Noise Floor [dB]"
   tl -352 400
   children {
    10
   }
   value -60
  }
  IOPInputValueClass {
   id 16
   name "Noise Bed Min [dB]"
   tl -352 560
   children {
    17
   }
   value -10
  }
 }
 Ops {
  IOPItemOpInterpolateClass {
   id 6
   name "TQ Crush Wet"
   tl 64 96
   children {
    5
   }
   inputs {
    ConnectionClass "1:0" {
     id 1
     port 0
    }
   }
   "Y min" 1
   "Y max" 0.2
  }
  IOPItemOpInterpolateClass {
   id 7
   name "TQ Noise Level"
   tl 64 272
   children {
    8
   }
   inputs {
    ConnectionClass "10:4" {
     id 10
     port 4
    }
    ConnectionClass "1:0" {
     id 1
     port 0
    }
   }
   "X max" 0.8
   "Y min" 1.8
   "Fade In Type" "Power of 1/3"
   "Fade Out Type" "Power of 1/3"
  }
  SignalOpDb2GainClass {
   id 10
   name "Noise Floor Gain"
   tl -160 400
   children {
    7 23
   }
   inputs {
    ConnectionClass "9:0" {
     id 9
     port 0
    }
   }
  }
  IOPItemOpInterpolateClass {
   id 14
   name "TQ Noise Bed Gain"
   tl 64 480
   children {
    12
   }
   inputs {
    ConnectionClass "17:3" {
     id 17
     port 3
    }
    ConnectionClass "1:0" {
     id 1
     port 0
    }
   }
  }
  SignalOpDb2GainClass {
   id 17
   name "Noise Bed Min Gain"
   tl -160 560
   children {
    14 24
   }
   inputs {
    ConnectionClass "16:0" {
     id 16
     port 0
    }
   }
  }
  IOPItemOpInterpolateClass {
   id 21
   name "Jam Crush Wet"
   tl 64 720
   children {
    25
   }
   inputs {
    ConnectionClass "20:0" {
     id 20
     port 0
    }
   }
   "Y min" 1
   "Y max" 0.2
  }
  IOPItemOpInterpolateClass {
   id 22
   name "Jam Voice Level"
   tl 64 880
   children {
    28
   }
   inputs {
    ConnectionClass "20:0" {
     id 20
     port 0
    }
   }
   "Y min" 0
   "Y max" 1
  }
  IOPItemOpInterpolateClass {
   id 23
   name "Jam Noise Level"
   tl 64 1040
   children {
    26
   }
   inputs {
    ConnectionClass "10:4" {
     id 10
     port 4
    }
    ConnectionClass "20:0" {
     id 20
     port 0
    }
   }
   "X max" 0.8
   "Y min" 1.8
   "Fade In Type" "Power of 1/3"
   "Fade Out Type" "Power of 1/3"
  }
  IOPItemOpInterpolateClass {
   id 24
   name "Jam Noise Bed Gain"
   tl 64 1200
   children {
    27
   }
   inputs {
    ConnectionClass "17:3" {
     id 17
     port 3
    }
    ConnectionClass "20:0" {
     id 20
     port 0
    }
   }
  }
 }
 Outputs {
  IOPItemOutputClass {
   id 5
   name "Quality_W"
   tl 288 96
   input 6
  }
  IOPItemOutputClass {
   id 8
   name "Noise_V"
   tl 288 272
   input 7
  }
  IOPItemOutputClass {
   id 12
   name "Radio_V"
   tl 288 480
   input 14
  }
  IOPItemOutputClass {
   id 25
   name "Jam_Quality_W"
   tl 288 720
   input 21
  }
  IOPItemOutputClass {
   id 26
   name "Jam_Noise_V"
   tl 288 1040
   input 23
  }
  IOPItemOutputClass {
   id 27
   name "Jam_Radio_V"
   tl 288 1200
   input 24
  }
  IOPItemOutputClass {
   id 28
   name "Jam_Voice_V"
   tl 288 880
   input 22
  }
 }
}