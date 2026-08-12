AudioSignalResClass {
 Inputs {
  IOPItemInputClass {
   id 1
   name "Signal Quality"
   tl -200 -1
   children {
    5 6 8
   }
  }
 }
 Ops {
  IOPItemOpInterpolateClass {
   id 5
   name "Clipper"
   tl 118 -154
   children {
    2
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
   id 6
   name "Low Pass FC"
   tl 124 236
   children {
    3
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
   id 8
   name "Radio Volume"
   tl 141.121 525.399
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
  }
 }
 Outputs {
  IOPItemOutputClass {
   id 2
   name "Clipper Drive"
   tl 476 -155
   input 5
  }
  IOPItemOutputClass {
   id 3
   name "Low Pass FC"
   tl 477 243
   input 6
  }
  IOPItemOutputClass {
   id 7
   name "Overall Volume"
   tl 506.121 556.399
   input 8
  }
 }
 Input_Order {
  ItemDetailListItemClass "Signal Quality" {
   Name "Signal Quality"
   Id 1
  }
 }
 Output_Order {
  ItemDetailListItemClass "Clipper Drive" {
   Name "Clipper Drive"
   Id 2
  }
  ItemDetailListItemClass "Low Pass FC" {
   Name "Low Pass FC"
   Id 3
  }
  ItemDetailListItemClass "Overall Volume" {
   Name "Overall Volume"
   Id 7
  }
 }
}