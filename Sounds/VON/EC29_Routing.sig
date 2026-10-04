AudioSignalResClass {
 Inputs {
  IOPInputVariableClass {
   id 1
   name "EC29_EarRouting"
   tl -320 96
   children {
    3 6
   }
   varName "EC29_EarRouting"
   varResource "{3DA1A848EE00C426}Sounds/VON/RadioEarRouting.conf"
  }
  IOPInputValueClass {
   id 2
   name "Right Code"
   tl -320 0
   children {
    3
   }
   value 1
  }
  IOPInputValueClass {
   id 8
   name "Ear Gain"
   tl -96 192
   children {
    7
   }
   value 2
  }
 }
 Ops {
  IOPItemOpSubClass {
   id 3
   name "Offset From Right"
   tl -128 32
   children {
    4
   }
   inputs {
    ConnectionClass "1:0" {
     id 1
     port 0
    }
    ConnectionClass "2:1" {
     id 2
     port 1
    }
   }
  }
  IOPItemOpAbsClass {
   id 4
   name "Distance From Right"
   tl 32 32
   children {
    7
   }
   inputs {
    ConnectionClass "3:0" {
     id 3
     port 0
    }
   }
  }
  IOPItemOpMulClass {
   id 7
   name "Left Gain"
   tl 192 64
   children {
    9
   }
   inputs {
    ConnectionClass "4:0" {
     id 4
     port 0
    }
    ConnectionClass "8:0" {
     id 8
     port 0
    }
   }
  }
  IOPItemOpInterpolateClass {
   id 6
   name "Right Gain"
   tl 64 256
   children {
    10
   }
   inputs {
    ConnectionClass "1:0" {
     id 1
     port 0
    }
   }
   "X min" 1
   "X max" 2
   "Y min" 2
   "Y max" 0
  }
 }
 Outputs {
  IOPItemOutputClass {
   id 9
   name "Left_G"
   tl 384 64
   input 7
  }
  IOPItemOutputClass {
   id 10
   name "Right_G"
   tl 384 256
   input 6
  }
 }
}