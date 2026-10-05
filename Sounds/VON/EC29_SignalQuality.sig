AudioSignalResClass {
 Inputs {
  IOPInputVariableClass {
   id 1
   name "EC29_SignalQuality"
   tl -256 160
   children {
    2 3 4
   }
   varName "EC29_SignalQuality"
   varResource "{3DA1A848EE00C426}Sounds/VON/RadioEarRouting.conf"
  }
 }
 Ops {
  IOPItemOpInterpolateClass {
   id 2
   name "Drive From Quality"
   tl 32 0
   children {
    5
   }
   inputs {
    ConnectionClass "1:0" {
     id 1
     port 0
    }
   }
   "Y min" 80
   "Y max" 20
  }
  IOPItemOpInterpolateClass {
   id 3
   name "Cutoff From Quality"
   tl 32 160
   children {
    6
   }
   inputs {
    ConnectionClass "1:0" {
     id 1
     port 0
    }
   }
   "Y min" 1000
   "Y max" 3600
  }
  IOPItemOpInterpolateClass {
   id 4
   name "Level From Quality"
   tl 32 320
   children {
    7
   }
   inputs {
    ConnectionClass "1:0" {
     id 1
     port 0
    }
   }
   "X max" 0.3
   "Y min" 0
   "Y max" 1
  }
 }
 Outputs {
  IOPItemOutputClass {
   id 5
   name "Clip_Drive"
   tl 256 0
   input 2
  }
  IOPItemOutputClass {
   id 6
   name "LP_Fc"
   tl 256 160
   input 3
  }
  IOPItemOutputClass {
   id 7
   name "Rx_V"
   tl 256 320
   input 4
  }
 }
}