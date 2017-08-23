#ifndef K_MG_H_
#define K_MG_H_

namespace K {
  static uv_timer_t mgStats_;
  static vector<mGWmt> mGWmt_;
  static json mGWmkt;
  static json mGWmktFilter;
  static double mgfairV = 0;
  static double mgEwmaL = 0;
  static double mgEwmaM = 0;
  static double mgEwmaS = 0;
  static double mgEwmaP = 0;
  static vector<double> mgSMA3;
  static vector<double> mgStatFV;
  static vector<double> mgStatBid;
  static vector<double> mgStatAsk;
  static vector<double> mgStatTop;
  static vector<double> mGWSMA33;    // Logging SMA3 values
  static vector<double> mSMATIME;    // logging SMA3 value timestamps
  static double mgStdevFV;
  static double mgStdevFVMean;
  static double mgStdevBid;
  static double mgStdevBidMean;
  static double mgStdevAsk;
  static double mgStdevAskMean;
  static double mgStdevTop;
  static double mgStdevTopMean;
  int mgT = 0;
  class MG {
    public:
      static void main(Local<Object> exports) {
        load();
        thread([&]() {
          if (uv_timer_init(uv_default_loop(), &mgStats_)) { cout << FN::uiT() << "Errrror: GW mgStats_ init timer failed." << endl; exit(1); }
          mgStats_.data = NULL;
          if (uv_timer_start(&mgStats_, [](uv_timer_t *handle) {
            if (mgfairV) {
              if (++mgT == 60) {
                mgT = 0;
                // updateEwmaValues();
                ewmaPUp();
              }
              stdevPUp();
            } else cout << FN::uiT() << "Market Stats notice: missing fair value." << endl;
          }, 0, 1000)) { cout << FN::uiT() << "Errrror: GW mgStats_ start timer failed." << endl; exit(1); }
        }).detach();
        EV::evOn("MarketTradeGateway", [](json k) {
                                mGWmt t(
                                        gw->exchange,
                                        gw->base,
                                        gw->quote,
                                        k["price"].get<double>(),
                                        k["size"].get<double>(),
                                        FN::T(),
                                        (mSide)k["make_side"].get<int>()
                                        );
                                mGWmt_.push_back(t);
                                if (mGWmt_.size()>69) mGWmt_.erase(mGWmt_.begin());
                                EV::evUp("MarketTrade");
                                UI::uiSend(uiTXT::MarketTrade, tradeUp(t));
                        });
        EV::evOn("MarketDataGateway", [](json o) {
                                mktUp(o);
                        });
        EV::evOn("GatewayMarketConnect", [](json k) {
                                if ((mConnectivityStatus)k["/0"_json_pointer].get<int>() == mConnectivityStatus::Disconnected)
                                        mktUp({});
                        });
        EV::evOn("QuotingParameters", [](json k) {
                                fairV();
                        });
        UI::uiSnap(uiTXT::MarketTrade, &onSnapTrade);
        UI::uiSnap(uiTXT::FairValue, &onSnapFair);
        NODE_SET_METHOD(exports, "mgFilter", MG::_mgFilter);
        NODE_SET_METHOD(exports, "mgFairV", MG::_mgFairV);
        NODE_SET_METHOD(exports, "mgEwmaLong", MG::_mgEwmaLong);
        NODE_SET_METHOD(exports, "mgEwmaMedium", MG::_mgEwmaMedium);
        NODE_SET_METHOD(exports, "mgEwmaShort", MG::_mgEwmaShort);
        NODE_SET_METHOD(exports, "mgTBP", MG::_mgTBP);
      };
    private:
      static void load() {
        json k = DB::load(uiTXT::EWMAChart);
        if (k.size()) {
          if (k["/0/fairValue"_json_pointer].is_number())
            mgfairV = k["/0/fairValue"_json_pointer].get<double>();
          if (k["/0/ewmaLong"_json_pointer].is_number())
            mgEwmaL = k["/0/ewmaLong"_json_pointer].get<double>();
          if (k["/0/ewmaMedium"_json_pointer].is_number())
            mgEwmaM = k["/0/ewmaMedium"_json_pointer].get<double>();
          if (k["/0/ewmaShort"_json_pointer].is_number())
            mgEwmaS = k["/0/ewmaShort"_json_pointer].get<double>();
        }
        json k_ = DB::load(uiTXT::MarketData);
        if (k_.size()) {
          for (json::iterator it = k_.begin(); it != k_.end(); ++it) {
            mgStatFV.push_back((*it)["fv"].get<double>());
            mgStatBid.push_back((*it)["bid"].get<double>());
            mgStatAsk.push_back((*it)["ask"].get<double>());
            mgStatTop.push_back((*it)["bid"].get<double>());
            mgStatTop.push_back((*it)["ask"].get<double>());
          }
          calcStdev();
        }
      };
      static void stdevPUp() {
        if (!mgfairV or empty()) return;
        mgStatFV.push_back(mgfairV);
        mgStatBid.push_back(mGWmktFilter["/bids/0/price"_json_pointer].get<double>());
        mgStatAsk.push_back(mGWmktFilter["/asks/0/price"_json_pointer].get<double>());
        mgStatTop.push_back(mGWmktFilter["/bids/0/price"_json_pointer].get<double>());
        mgStatTop.push_back(mGWmktFilter["/asks/0/price"_json_pointer].get<double>());
        calcStdev();
        DB::insert(uiTXT::MarketData, {
          {"fv", mgfairV},
          {"bid", mGWmktFilter["/bids/0/price"_json_pointer].get<double>()},
          {"ask", mGWmktFilter["/bids/0/price"_json_pointer].get<double>()},
          {"time", FN::T()},
        }, false, "NULL", FN::T() - 1000 * qpRepo["quotingStdevProtectionPeriods"].get<int>());
      };
      static void _mgEwmaProtection(const FunctionCallbackInfo<Value>& args) {
        args.GetReturnValue().Set(Number::New(args.GetIsolate(), mgEwmaP));
      };
      static void _mgStdevProtection(const FunctionCallbackInfo<Value>& args) {
        Isolate* isolate = args.GetIsolate();
        HandleScope scope(isolate);
        Local<Object> o = Object::New(isolate);
        o->Set(FN::v8S("fv"), Number::New(isolate, mgStdevFV));
        o->Set(FN::v8S("fvMean"), Number::New(isolate, mgStdevFVMean));
        o->Set(FN::v8S("tops"), Number::New(isolate, mgStdevTop));
        o->Set(FN::v8S("topsMean"), Number::New(isolate, mgStdevTopMean));
        o->Set(FN::v8S("bid"), Number::New(isolate, mgStdevBid));
        o->Set(FN::v8S("bidMean"), Number::New(isolate, mgStdevBidMean));
        o->Set(FN::v8S("ask"), Number::New(isolate, mgStdevAsk));
        o->Set(FN::v8S("askMean"), Number::New(isolate, mgStdevAskMean));
        args.GetReturnValue().Set(o);
      };
      static void _mgEwmaLong(const FunctionCallbackInfo<Value>& args) {
        mgEwmaL = calcEwma(args[0]->NumberValue(), mgEwmaL, qpRepo["longEwmaPeridos"].get<int>());
        args.GetReturnValue().Set(Number::New(args.GetIsolate(), mgEwmaL));
      };
      static void _mgEwmaMedium(const FunctionCallbackInfo<Value>& args) {
        mgEwmaM = calcEwma(args[0]->NumberValue(), mgEwmaM, qpRepo["mediumEwmaPeridos"].get<int>());
        args.GetReturnValue().Set(Number::New(args.GetIsolate(), mgEwmaM));
      };
      static void _mgEwmaShort(const FunctionCallbackInfo<Value>& args) {
        mgEwmaS = calcEwma(args[0]->NumberValue(), mgEwmaS, qpRepo["shortEwmaPeridos"].get<int>());
        args.GetReturnValue().Set(Number::New(args.GetIsolate(), mgEwmaS));
      };
      static void _mgTBP(const FunctionCallbackInfo<Value>& args) {
        mgSMA3.push_back(args[0]->NumberValue());
        double newLong = args[1]->NumberValue();
        double newMedium = args[2]->NumberValue();
        double newShort = args[3]->NumberValue();
	        printf("Short: %f Medium: %f Long: %f \n", newShort, newMedium, newLong);

        if (mgSMA3.size()>3) mgSMA3.erase(mgSMA3.begin(), mgSMA3.end()-3);
        double SMA3 = 0;
	  double SMA33 = 0;
        for (vector<double>::iterator it = mgSMA3.begin(); it != mgSMA3.end(); ++it)
          SMA3 += *it;
        SMA3 /= mgSMA3.size();

  unsigned long int SMA33STARTTIME = std::time(nullptr); // get the time since EWMAProtectionCalculator

        // lets make a SMA logging average
        mGWSMA33.push_back(SMA3);
        if (mGWSMA33.size()>100) mGWSMA33.erase(mGWSMA33.begin(), mGWSMA33.end()-1);
	
	mSMATIME.push_back((int)SMA33STARTTIME);
        if (mSMATIME.size()>100) mSMATIME.erase(mSMATIME.begin(), mSMATIME.end()-1);

        double newTargetPosition = 0;
        if ((mAutoPositionMode)qpRepo["autoPositionMode"].get<int>() == mAutoPositionMode::EWMA_LMS) {
                double newTrend = ((SMA3 * 100 / newLong) - 100);
                double newEwmacrossing = ((newShort * 100 / newMedium) - 100);
                newTargetPosition = ((newTrend + newEwmacrossing) / 2) * (1 / qpRepo["ewmaSensiblityPercentage"].get<double>());
                qpRepo["aspvalue"] = newTargetPosition;
        } else if ((mAutoPositionMode)qpRepo["autoPositionMode"].get<int>() == mAutoPositionMode::EWMA_LS) {
                newTargetPosition = ((newShort * 100/ newLong) - 100) * (1 / qpRepo["ewmaSensiblityPercentage"].get<double>());
                qpRepo["aspvalue"] = newTargetPosition;
        //        printf("ASP: value: %f\n", newTargetPosition);
        }
      //  newTargetPosition = ((newShort * 100/ newLong) - 100) * (1 / qpRepo["ewmaSensiblityPercentage"].get<double>());
      //  printf("ASP: ewma?: %f", qpRepo["ewmaSensiblityPercentage"].get<double>() );
      //  qpRepo["aspvalue"] = newTargetPosition;
      //  printf("ASP: value: %f\n", newTargetPosition);
      //  printf("ASP: value: %f\n", qpRepo["aspvalue"].get<double>());
        printf("ASP: testvalue: %f\n", (newShort * 100/ newLong) - 100);


        if (newTargetPosition > 1) newTargetPosition = 1;
        else if (newTargetPosition < -1) newTargetPosition = -1;

        if ( (qpRepo["aspvalue"].get<double>() >= qpRepo["asp_high"].get<double>() || qpRepo["aspvalue"].get<double>() <= qpRepo["asp_low"].get<double>()) && qpRepo["aspactive"].get<bool>() == true ) {
                //  pdiv changes here..
        }
        printf("ASP: ASPVALUE %f  ASPHIGH: %f ASPLOW: %f  ASPACTIVE: %d\n", qpRepo["aspvalue"].get<double>(), qpRepo["asp_high"].get<double>(), qpRepo["asp_low"].get<double>(), qpRepo["aspactive"].get<bool>()  );
        // relocating this for now...  args.GetReturnValue().Set(Number::New(args.GetIsolate(), newTargetPosition));
        // lets do some SMA math to see if we can buy or sell safety time!
        //  printf("SMA33 Buy Mode Active  First Value: %f  Last Value %f safetyPercent: %f \n", mGWSMA33.back(), mGWSMA33.front(), qpRepo["safetyP"].get<double>()/100);
          printf("SMA33 Debugging:  Is SafetyActive: %d  Is Safety Even On: %d\n",qpRepo["safetyactive"].get<bool>(),qpRepo["safetynet"].get<bool>()  );
          printf("Array size %lu \n", mGWSMA33.size() );

        if(mGWSMA33.size() > 3) {
                if (
                       ( mGWSMA33.back() * 100 / mGWSMA33.front() - 100 >  qpRepo["safetyP"].get<double>()/100 )
                        &&  qpRepo["safetyactive"].get<bool>() == false
                        &&  qpRepo["safetynet"].get<bool>() == true
              )
              {
                      //  printf("debug12\n");
                        // activate Safety, Safety buySize
                        qpRepo["mSafeMode"] = (int)mSafeMode::buy;
                        qpRepo["safetyactive"] = true;
                        qpRepo["safetimestart"] = (unsigned long int)SMA33STARTTIME;
                        printf("SMA33 Buy Mode Active  First Value: %f  Last Value %f safetyPercent: %f \n", mGWSMA33.back(), mGWSMA33.front(), qpRepo["safetyP"].get<double>()/100);
                        printf("SMA33 Start Time started at: %lu \n", qpRepo["safetimestart"].get<unsigned long int>());
                        //  qpRepo["safetyduration"] = std::time(nullptr) + qpRepo["safetimestart"].get<unsigned long int>();
                        qpRepo["safetyduration"] = qpRepo["safetimestart"].get<unsigned long int>() + (qpRepo["safetimeOver"].get<unsigned long int>() * 60000);
                }
                if (  (mGWSMA33.back() * 100 / mGWSMA33.front() - 100 <  qpRepo["safetyP"].get<double>()/100 )
                &&   qpRepo["safetyactive"].get<bool>() == false
                &&   qpRepo["safetynet"].get<bool>() == true
                )
                {
                        printf("debug13\n");
                        qpRepo["mSafeMode"] = (int)mSafeMode::sell;
                        qpRepo["safetimestart"] = (unsigned long int)SMA33STARTTIME;
                        qpRepo["safetyactive"] = true;
                        printf("SMA33 Sell Mode Active: First Value: %f  Last Value %f safetyPercent: %f \n", mGWSMA33.back(), mGWSMA33.front(),qpRepo["safetyP"].get<double>()/100 );
                        printf("SMA33 Start Time started at: %lu \n", qpRepo["safetimestart"].get<unsigned long int>());
                         qpRepo["safetyduration"] = qpRepo["safetimestart"].get<unsigned long int>() + (qpRepo["safetimeOver"].get<unsigned long int>() * 60000);
                }

        }

    //   if(qpRepo["safetyactive"].get<bool>() == true )
    //   {
    //     qpRepo["safetyduration"] = std::time(nullptr) - qpRepo["safetimestart"].get<unsigned long int>() ;
    //   }

        printf("Duration: %lu  Start time: %lu Time Starated: %lu\n", qpRepo["safetyduration"].get<unsigned long int>(), std::time(nullptr), qpRepo["safetimestart"].get<unsigned long int>() );
        if(mGWSMA33.size() > 3  ) {
                printf("Debug1\n");
                if(mGWSMA33.size() > qpRepo["safetytime"].get<double>() and qpRepo["safetyactive"].get<bool>() == true )// checking to make sure array size is larger than what we are looking for.. otherwise.. KABOOOM!
                {
                        printf("debug2\n");
                        //  if( (mGWSMA33.back() < mGWSMA33.at(mGWSMA33.size() - qpRepo["safetytime"].get<int>()) ) && (qpRepo["safetyduration"].get<unsigned long int>() >= (qpRepo["safetimeOver"].get<unsigned long int>() * 60000)))
                      //  printf("arraySize: %lu\n", mGWSMA33.size() );
                        //if( mGWSMA33.back() > mGWSMA33.at(mGWSMA33.size() - qpRepo["safetytime"].get<double>()) )
                                if((mGWSMA33.back() < mGWSMA33.at(mGWSMA33.size() - qpRepo["safetytime"].get<int>()) ) or ( std::time(nullptr) > qpRepo["safetyduration"].get<unsigned long int>()  ) )
                                {
                                        printf("debug3\n");
                                        qpRepo["mSafeMode"] = (int)mSafeMode::unknown;
                                        qpRepo["safetyactive"] = false;
                                        printf("SMA33 Safety Mode is over \n");
                                        printf("debug4\n");
                                }
                      //  printf("debugzz\n");
                        //  double spacer = mGWSMA33.at(mGWSMA33.size() - qpRepo["safetytime"].get<double>()).get<double>();
                        //if( (mGWSMA33.back() > mGWSMA33.at(mGWSMA33.size() - qpRepo["safetytime"].get<int>()) ) && (qpRepo["safetyduration"].get<unsigned long int>() >= (qpRepo["safetimeOver"].get<unsigned long int>() * 60000)))
                        if( (mGWSMA33.back() > mGWSMA33.at(mGWSMA33.size() - qpRepo["safetytime"].get<double>())  ) or  ( std::time(nullptr) > qpRepo["safetyduration"].get<unsigned long int>() ) )
                        {
                                printf("Current Short: %f   old Short: %f\n", mGWSMA33.back(),  mGWSMA33.at(mGWSMA33.size() - qpRepo["safetytime"].get<double>()));
                              //  printf("debug5\n");
                                qpRepo["mSafeMode"] = (int)mSafeMode::unknown;
                                qpRepo["safetyactive"] = false;
                                printf("SMA33 Safety Mode is over \n");
                              //  printf("debug6\n");
                        }
                }
        }

        // printf("safetyactive: %d safetynet: %d safemode: %d\n", qpRepo["safetyactive"].get<bool>(), qpRepo["safetynet"].get<bool>(), qpRepo["mSafeMode"].get<int>() );
        if( qpRepo["safetyactive"].get<bool>() == true and qpRepo["safetynet"].get<bool>() == true and (mSafeMode) qpRepo["safemode"].get<int>() == mSafeMode::buy)
        {
                newTargetPosition = 1;
        } else if( qpRepo["safetyactive"].get<bool>() == true and qpRepo["safetynet"].get<bool>() == true and (mSafeMode) qpRepo["safemode"].get<int>() == mSafeMode::sell )
        {
                newTargetPosition = -1;
        }

        //      if (o["computationalLatency"].is_null() and (mORS)o["orderStatus"].get<int>() == mORS::Working)
        args.GetReturnValue().Set(Number::New(args.GetIsolate(), newTargetPosition));






};




static void _mgFilter(const FunctionCallbackInfo<Value>& args) {
        Isolate* isolate = args.GetIsolate();
        JSON Json;
        args.GetReturnValue().Set(Json.Parse(isolate->GetCurrentContext(), FN::v8S(isolate, mGWmktFilter.dump())).ToLocalChecked());
      };
      static void _mgFairV(const FunctionCallbackInfo<Value>& args) {
        args.GetReturnValue().Set(Number::New(args.GetIsolate(), mgfairV));
      };
      static json onSnapTrade(json z) {
        json k;
        for (unsigned i=0; i<mGWmt_.size(); ++i)
                k.push_back(tradeUp(mGWmt_[i]));
        return k;
      };
      static json onSnapFair(json z) {
        return {{{"price", mgfairV}}};
      };
      static void mktUp(json k) {
        mGWmkt = k;
        filter();
        UI::uiSend(uiTXT::MarketData, k, true);
};
static json tradeUp(mGWmt t) {
        json o = {
                {"exchange", (int)t.exchange},
                {"pair", {{"base", t.base}, {"quote", t.quote}}},
                {"price", t.price},
                {"size", t.size},
                {"time", t.time},
                {"make_size", (int)t.make_side}
        };
        return o;
      };
      static void ewmaPUp() {
        mgEwmaP = calcEwma(mgfairV, mgEwmaP, qpRepo["quotingEwmaProtectionPeridos"].get<int>());
        EV::evUp("EWMAProtectionCalculator");
      };
      static bool empty() {
        return (mGWmktFilter.is_null()
          or mGWmktFilter["bids"].is_null()
          or mGWmktFilter["asks"].is_null()
        );
      };
      static void filter() {
        mGWmktFilter = mGWmkt;
        if (empty()) return;
        for (map<string, json>::iterator it = allOrders.begin(); it != allOrders.end(); ++it)
          filter(mSide::Bid == (mSide)it->second["side"].get<int>() ? "bids" : "asks", it->second);
        if (!empty()) {
          fairV();
          EV::evUp("FilteredMarket");
        }
      };
      static void filter(string k, json o) {
        for (json::iterator it = mGWmktFilter[k].begin(); it != mGWmktFilter[k].end();)
          if (abs((*it)["price"].get<double>() - o["price"].get<double>()) < gw->minTick) {
            (*it)["size"] = (*it)["size"].get<double>() - o["quantity"].get<double>();
            if ((*it)["size"].get<double>() < gw->minTick) mGWmktFilter[k].erase(it);
            break;
          } else ++it;
      };
      static void fairV() {
        // if (mGWmktFilter.is_null() or mGWmktFilter["/bids/0"_json_pointer].is_null() or mGWmktFilter["/asks/0"_json_pointer].is_null()) return;
        if (empty()) return;
        double mgfairV_ = mgfairV;
        mgfairV = FN::roundNearest(
          mFairValueModel::BBO == (mFairValueModel)qpRepo["fvModel"].get<int>()
            ? (mGWmktFilter["/asks/0/price"_json_pointer].get<double>() + mGWmktFilter["/bids/0/price"_json_pointer].get<double>()) / 2
            : (mGWmktFilter["/asks/0/price"_json_pointer].get<double>() * mGWmktFilter["/asks/0/size"_json_pointer].get<double>() + mGWmktFilter["/bids/0/price"_json_pointer].get<double>() * mGWmktFilter["/bids/0/size"_json_pointer].get<double>()) / (mGWmktFilter["/asks/0/size"_json_pointer].get<double>() + mGWmktFilter["/bids/0/size"_json_pointer].get<double>()),
          gw->minTick
        );
        if (!mgfairV or (mgfairV_ and abs(mgfairV - mgfairV_) < gw->minTick)) return;
        EV::evUp("FairValue");
        UI::uiSend(uiTXT::FairValue, {{"price", mgfairV}}, true);
      };
      static void cleanStdev() {
        int periods = qpRepo["quotingStdevProtectionPeriods"].get<int>();
        if (mgStatFV.size()>periods) mgStatFV.erase(mgStatFV.begin(), mgStatFV.end()-periods);
        if (mgStatBid.size()>periods) mgStatBid.erase(mgStatBid.begin(), mgStatBid.end()-periods);
        if (mgStatAsk.size()>periods) mgStatAsk.erase(mgStatAsk.begin(), mgStatAsk.end()-periods);
        if (mgStatTop.size()>periods*2) mgStatTop.erase(mgStatTop.begin(), mgStatTop.end()-(periods*2));
      };
      static void calcStdev() {
        cleanStdev();
        if (mgStatFV.size() < 2 or mgStatBid.size() < 2 or mgStatAsk.size() < 2 or mgStatTop.size() < 4) return;
        double factor = qpRepo["quotingStdevProtectionFactor"].get<double>();
        double mean = 0;
        mgStdevFV = calcStdev(mgStatFV, mgStatFV.size(), factor, &mean);
        mgStdevFVMean = mean;
        mgStdevBid = calcStdev(mgStatBid, mgStatBid.size(), factor, &mean);
        mgStdevBidMean = mean;
        mgStdevAsk = calcStdev(mgStatAsk, mgStatAsk.size(), factor, &mean);
        mgStdevAskMean = mean;
        mgStdevTop = calcStdev(mgStatTop, mgStatTop.size(), factor, &mean);
        mgStdevTopMean = mean;
      };
      static double calcStdev(vector<double> a, int n, double f, double *mean) {
        if (n == 0) return 0.0;
        double sum = 0;
        for (int i = 0; i < n; ++i) sum += a[i];
        *mean = sum / n;
        double sq_diff_sum = 0;
        for (int i = 0; i < n; ++i) {
          double diff = a[i] - *mean;
          sq_diff_sum += diff * diff;
        }
        double variance = sq_diff_sum / n;
        return sqrt(variance) * f;
      };
      static double calcEwma(double newValue, double previous, int periods) {
        if (previous) {
                double alpha = 2 / (periods + 1);
                return alpha * newValue + (1 - alpha) * previous;
        }
        return newValue;
};
};
}

#endif
