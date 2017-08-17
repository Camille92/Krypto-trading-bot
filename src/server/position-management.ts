import Models = require("../share/models");
import Utils = require("./utils");
import Statistics = require("./statistics");
import FairValue = require("./fair-value");
import moment = require("moment");
import Broker = require("./broker");

export class TargetBasePositionManager {
  public sideAPR: string;

  private newWidth: Models.IStdev = null;
  private newQuote: number = null;
  private newShort: number = null;
  private newMedium: number = null;
  private newLong: number = null;
  private fairValue: number = null;
  public set quoteEwma(quoteEwma: number) {
    this.newQuote = quoteEwma;
  }
  public set widthStdev(widthStdev: Models.IStdev) {
    this.newWidth = widthStdev;
  }

  private _newTargetPosition: number = 0;
  private _lastPosition: number = null;

  private _latest: Models.TargetBasePositionValue = null;
  public get latestTargetPosition(): Models.TargetBasePositionValue {
    return this._latest;
  }

  constructor(
    private _minTick: number,
    private _dbInsert,
    private _fvAgent: FairValue.FairValueEngine,
    private _ewma: Statistics.EWMATargetPositionCalculator,
    private _qpRepo,
    private _positionBroker: Broker.PositionBroker,
    private _uiSnap,
    private _uiSend,
    private _evOn,
    private _evUp,
    initTBP: Models.TargetBasePositionValue[]
  ) {
    if (initTBP.length && typeof initTBP[0].tbp != "undefined") {
      this._latest = initTBP[0];
      console.info(new Date().toISOString().slice(11, -1), 'tbp', 'Loaded from DB:', this._latest.tbp);
    }

    _uiSnap(Models.Topics.TargetBasePosition, () => [this._latest]);
    _uiSnap(Models.Topics.EWMAChart, () => [this.fairValue?new Models.EWMAChart(
      this.newWidth,
      this.newQuote?Utils.roundNearest(this.newQuote, this._minTick):null,
      this.newShort?Utils.roundNearest(this.newShort, this._minTick):null,
      this.newMedium?Utils.roundNearest(this.newMedium, this._minTick):null,
      this.newLong?Utils.roundNearest(this.newLong, this._minTick):null,
      this.fairValue?Utils.roundNearest(this.fairValue, this._minTick):null
    ):null]);
    this._evOn('PositionBroker', this.recomputeTargetPosition);
    this._evOn('QuotingParameters', () => setTimeout(() => this.recomputeTargetPosition(), moment.duration(121)));
    setInterval(this.updateEwmaValues, moment.duration(1, 'minutes'));
  }

  private recomputeTargetPosition = () => {
    const params = this._qpRepo();
    if (params === null || this._positionBroker.latestReport === null) {
      console.info(new Date().toISOString().slice(11, -1), 'tbp', 'Unable to compute tbp [ qp | pos ] = [', !!params, '|', !!this._positionBroker.latestReport, ']');
      return;
    }
    const targetBasePosition: number = (params.autoPositionMode === Models.AutoPositionMode.Manual)
      ? (params.percentageValues
        ? params.targetBasePositionPercentage * this._positionBroker.latestReport.value / 100
        : params.targetBasePosition)
      : ((1 + this._newTargetPosition) / 2) * this._positionBroker.latestReport.value;

    if (this._latest === null || Math.abs(this._latest.tbp - targetBasePosition) > 1e-4 || this.sideAPR !== this._latest.sideAPR) {
      this._latest = new Models.TargetBasePositionValue(targetBasePosition, this.sideAPR);
      this._evUp('TargetPosition');
      this._uiSend(Models.Topics.TargetBasePosition, this._latest, true);
      this._dbInsert(Models.Topics.TargetBasePosition, this._latest);
      console.info(new Date().toISOString().slice(11, -1), 'tbp', 'recalculated', this._latest.tbp);

      this.fairValue = this._fvAgent.latestFairValue.price;



      params.aspvalue = (this._ewma.latestShort * 100 / this._ewma.latestLong) - 100 ;
      console.info(new Date().toISOString().slice(11, -1), 'ASP2', 'Fair Value:', this.fairValue  )
      console.info(new Date().toISOString().slice(11, -1), 'ASP2', 'New Short Value:', (this._ewma.latestShort * 100) )
      console.info(new Date().toISOString().slice(11, -1), 'ASP2', 'New Long Value:', (this._ewma.latestLong ) )
      console.info(new Date().toISOString().slice(11, -1), 'ASP2', 'recalculated', params.aspvalue )

      if(this._ewma.latestShort  > this._ewma.latestLong) {
        // Going up!
        params.moveit = Models.mMoveit.up;
        console.info(new Date().toISOString().slice(11, -1), 'MoveMent: ',   Models.mMoveit[params.moveit] )

      } else if(this._ewma.latestShort  < this._ewma.latestLong) {
        // Going down
        params.moveit = Models.mMoveit.down;
        console.info(new Date().toISOString().slice(11, -1), 'MoveMent: ',   Models.mMoveit[params.moveit] )
      }


    }
  };

  private updateEwmaValues = () => {
    if (this._fvAgent.latestFairValue === null) {
      console.info(new Date().toISOString().slice(11, -1), 'tbp', 'Unable to update ewma');
      return;
    }
    this.fairValue = this._fvAgent.latestFairValue.price;

    this.newShort = this._ewma.addNewShortValue(this.fairValue);
    this.newMedium = this._ewma.addNewMediumValue(this.fairValue);
    this.newLong = this._ewma.addNewLongValue(this.fairValue);
    this._newTargetPosition = this._ewma.computeTBP(this.fairValue, this.newLong, this.newMedium, this.newShort);
    // console.info(new Date().toISOString().slice(11, -1), 'tbp', 'recalculated ewma [ FV | L | M | S ] = [',this.fairValue,'|',this.newLong,'|',this.newMedium,'|',this.newShort,']');
    this.recomputeTargetPosition();

    this._uiSend(Models.Topics.EWMAChart, new Models.EWMAChart(
      this.newWidth,
      this.newQuote?Utils.roundNearest(this.newQuote, this._minTick):null,
      Utils.roundNearest(this.newShort, this._minTick),
      Utils.roundNearest(this.newMedium, this._minTick),
      Utils.roundNearest(this.newLong, this._minTick),
      Utils.roundNearest(this.fairValue, this._minTick)
    ), true);

    this._dbInsert(Models.Topics.EWMAChart, new Models.RegularFairValue(this.fairValue, this.newLong, this.newMedium, this.newShort));
  };
}
