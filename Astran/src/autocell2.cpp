/**************************************************************************
 *   Copyright (C) 2005 by Adriel Mota Ziesemer Jr.                        *
 *   amziesemerj[at]inf.ufrgs.br                                           *
 ***************************************************************************/
#include "autocell2.h"

AutoCell::AutoCell() {
    currentCell = NULL;
    rt = NULL;
    state = 0;
}

AutoCell::~AutoCell() {
    clear();
}

void AutoCell::clear() {
    elements.clear();
    inoutPins.clear();
    if (rt != NULL) delete(rt);
}

Element* AutoCell::createElement(int vcost, int nDiffIni, int pDiffIni) {
    Element tmp;
    tmp.diffP = rt->createNode();
    tmp.diffN = rt->createNode();
    tmp.linkP.type = tmp.linkN.type = GAP;
    tmp.gapP = tmp.gapN = false;
    tmp.met.resize(trackPos.size());
    tmp.pol.resize(trackPos.size());
    
    for (int x = 0; x < trackPos.size(); x++) {
        tmp.met[x] = rt->createNode();
        if (x) rt->addArc(tmp.met[x], tmp.met[x - 1], 5);
        if (elements.size())
            rt->addArc(tmp.met[x], elements.back().met[x], 4); //if it's not the first, connect to the last element
        
        tmp.pol[x] = rt->createNode();
        if (x) rt->addArc(tmp.pol[x], tmp.pol[x - 1], 5);
        if ((x && x > nDiffIni && x < pDiffIni) || (false)) { //Correct to insert area outside the transistors
            rt->addArc(tmp.pol[x], tmp.met[x], 20);
            if (elements.size() && elements.back().pol[x] != -1)
                rt->addArc(tmp.pol[x], elements.back().pol[x], 5); //if it's not the first, connect to the last element
        }
    }
    rt->addNodetoNet(gnd, tmp.met[0]);
    rt->addNodetoNet(vdd, tmp.met[trackPos.size() - 1]);
    
    tmp.inoutCnt = rt->createNode();
    
    elements.push_back(tmp);
    return &elements.back();
}

bool AutoCell::calcArea(Circuit* c) {
    currentCircuit = c;
    currentRules = currentCircuit->getRules();
    
    cout << "Calculating cell area..." << endl;
    state = 0;
    
    vGrid = currentRules->getIntValue(currentCircuit->getVPitch());
    hGrid = currentRules->getIntValue(currentCircuit->getHPitch());
    height = currentCircuit->getRowHeight() * vGrid;
    
    supWidth = max(currentRules->getIntValue(currentCircuit->getSupplyVSize()), currentRules->getRule(W1M1)) / 2;
    posNWell = height / 2; //Improve!!!
    pDif_iniY = posNWell + currentRules->getRule(E1WNDP);
    nDif_iniY = posNWell - currentRules->getRule(S1DNWN);
    trackPos.clear();
    //central track position
    center = round((float(posNWell) / vGrid) - 0.5);
    trackPos.push_back(center * vGrid);
    // cout << center << endl;
    
    //tracks position under the central track
    int next;
    do {
        next = trackPos.front() - currentRules->getRule(S1M1M1) - currentRules->getRule(W1M1);
        trackPos.insert(trackPos.begin(), next);
    } while (next >= supWidth / 2 + currentRules->getRule(S1M1M1));
    
    //tracks position above the central track
    do {
        next = trackPos.back() + currentRules->getRule(S1M1M1) + currentRules->getRule(W1M1); //IMPROVE!!!
        trackPos.push_back(next);
    } while (next <= height - supWidth / 2 - currentRules->getRule(S1M1M1));
    
    for (int x = 0; x < trackPos.size(); x++) cout << float(trackPos[x]) / currentRules->getScale() << " ";
    
    nDif_iniY = min(nDif_iniY, center * vGrid - (currentRules->getRule(W2CT) / 2 + currentRules->getRule(E2P1CT) + currentRules->getRule(S1DFP1)));
    pDif_iniY = max(pDif_iniY, center * vGrid + (currentRules->getRule(W2CT) / 2 + currentRules->getRule(E2P1CT) + currentRules->getRule(S1DFP1)));
    
    nDif_endY = max(currentRules->getRule(E1INDF), currentRules->getRule(S1P1P1) / 2 + currentRules->getRule(E1P1DF));
    pDif_endY = height - max(currentRules->getRule(E1IPDF), currentRules->getRule(S1P1P1) / 2 + currentRules->getRule(E1P1DF));
    
    nSize = nDif_iniY - nDif_endY;
    pSize = pDif_endY - pDif_iniY;
    
    cout << "Resume: tracks(" << trackPos.size() << ") " << nSize << " " << pSize << endl;
    return state++;
}

bool AutoCell::selectCell(string c) {
    if (state < 1) return 0;
    state = 1;
    
    if (currentCell = currentCircuit->getCellNetlst(c)) {
        cout << "Selecting cell netlist: " << currentCell->getName() << endl;
        currentNetList.clear();
        currentNetList = currentCircuit->getFlattenCell(c);
        state = 2;
    }
    return state == 2;
}

bool AutoCell::foldTrans() {
    cout << "Applying folding..." << endl;
    if (state < 2) return 0;
    state = 2;
    cout << "- Number of transistors before folding: " << currentCell->size() << " -> P(" << currentCell->pSize() << ") N(" << currentCell->nSize() << ")" << endl;
    if (currentNetList.folding(float(pSize) / currentRules->getScale(), float(nSize) / currentRules->getScale())) {
        cout << "- Number of transistors after folding: " << currentNetList.size() << " -> P(" << currentNetList.pSize() << ") N(" << currentNetList.nSize() << ")" << endl;
        state = 3;
    }
    
    
    return state == 3;
}

bool AutoCell::placeTrans(bool ep, int saquality, int nrAttempts, int wC, int gmC, int rC, int congC, int ngC) {
    cout << "Placing transistors..." << endl;
    if (state < 3) return 0;
    state = 3;
    if (currentNetList.transPlacement(ep, saquality, nrAttempts, wC, gmC, rC, congC, ngC)) {
        // CALCULATE THE NR OF INTERNAL TRACKS
        diffPini.clear();
        diffNini.clear();
        for (int x = 0; x < currentNetList.getOrderingP().size(); x++) {
            int p;
            for (p = center + 1; trackPos[p] < pDif_iniY; ++p) {
            }
            diffPini.push_back(p);
        }
        for (int x = 0; x < currentNetList.getOrderingN().size(); x++) {
            int p;
            for (p = center - 1; trackPos[p] > nDif_iniY; --p) {
            }
            diffNini.push_back(p);
        }
        
        // CONSTRUCT THE ROUTING GRAPH
        int COST_CNT_INSIDE_DIFF = 10, diffCost;
        
        Element *tmp, *lastElement;
        bool gapP, gapN, decCostIOP, decCostION;
        
        clear();
        rt = new Pathfinder2();
        
        currentCell->print();
        vector<int>::iterator inouts_it = currentNetList.getInouts().begin();
        while (inouts_it != currentNetList.getInouts().end()) {
            //		cerr << currentCircuit->getGndNet()  << " - " <<  currentCircuit->getVddNet() << " - " << currentNetList.getNetName(*inouts_it) << endl;
            if (currentNetList.getNetName(*inouts_it) == currentCircuit->getVddNet()) {
                vdd = *inouts_it;
            } else if (currentNetList.getNetName(*inouts_it) == currentCircuit->getGndNet()) {
                gnd = *inouts_it;
            } else {
                inoutPins[currentNetList.getNetName(*inouts_it)] = rt->createNode();
                rt->addNodetoNet(*inouts_it, inoutPins[currentNetList.getNetName(*inouts_it)]);
            }
            inouts_it++;
        }
        //cout << currentNetList.getNetName(0);
        
        //cria elemento para roteamento lateral
        tmp = createElement(20, center, center);
        rt->addArc(tmp->inoutCnt, tmp->met[center], 550);
        
        //conecta sinais de entrada e saida com o nó inoutCnt do elemento
        map<string, int>::iterator inoutPins_it;
        for (inoutPins_it = inoutPins.begin(); inoutPins_it != inoutPins.end(); inoutPins_it++)
            rt->addArc(tmp->inoutCnt, inoutPins_it->second, 0);
        
        decCostIOP = decCostION = true;
        vector<t_net2>::iterator eulerPathP_it = currentNetList.getOrderingP().begin(), eulerPathN_it = currentNetList.getOrderingN().begin(), lastP_it, lastN_it;
        
        while (eulerPathP_it != currentNetList.getOrderingP().end() && eulerPathN_it != currentNetList.getOrderingN().end()) {
            gapP = false;
            gapN = false;
            
            if (eulerPathP_it != currentNetList.getOrderingP().begin() && eulerPathP_it->link != -1 && tmp->linkP.type != GAP) {
                if ((lastP_it->type == SOURCE && (
                                                  (eulerPathP_it->type == SOURCE && currentNetList.getTrans(lastP_it->link).drain != currentNetList.getTrans(eulerPathP_it->link).source) ||
                                                  (eulerPathP_it->type == DRAIN && currentNetList.getTrans(lastP_it->link).drain != currentNetList.getTrans(eulerPathP_it->link).drain))) ||
                    (lastP_it->type == DRAIN && (
                                                 (eulerPathP_it->type == SOURCE && currentNetList.getTrans(lastP_it->link).source != currentNetList.getTrans(eulerPathP_it->link).source) ||
                                                 (eulerPathP_it->type == DRAIN && currentNetList.getTrans(lastP_it->link).source != currentNetList.getTrans(eulerPathP_it->link).drain))) ||
                    eulerPathP_it->link == -1)
                    gapP = true;
            }
            if (eulerPathN_it != currentNetList.getOrderingN().begin() && eulerPathN_it->link != -1 && tmp->linkN.type != GAP) {
                if ((lastN_it->type == SOURCE && (
                                                  (eulerPathN_it->type == SOURCE && currentNetList.getTrans(lastN_it->link).drain != currentNetList.getTrans(eulerPathN_it->link).source) ||
                                                  (eulerPathN_it->type == DRAIN && currentNetList.getTrans(lastN_it->link).drain != currentNetList.getTrans(eulerPathN_it->link).drain))) ||
                    (lastN_it->type == DRAIN && (
                                                 (eulerPathN_it->type == SOURCE && currentNetList.getTrans(lastN_it->link).source != currentNetList.getTrans(eulerPathN_it->link).source) ||
                                                 (eulerPathN_it->type == DRAIN && currentNetList.getTrans(lastN_it->link).source != currentNetList.getTrans(eulerPathN_it->link).drain))) ||
                    eulerPathN_it->link == -1)
                    gapN = true;
            }
            
            // DIFF
            if (gapP || gapN || eulerPathP_it == currentNetList.getOrderingP().begin() || eulerPathN_it == currentNetList.getOrderingN().begin()) {
                lastElement = tmp;
                tmp = createElement(5, diffNini[eulerPathN_it - currentNetList.getOrderingN().begin()], diffPini[eulerPathP_it - currentNetList.getOrderingP().begin()]);
                
                //conecta aos pinos de entrada e saida
                rt->addArc(tmp->inoutCnt, tmp->met[center], 496);
                
                decCostIOP = decCostION = false;
                
                //conecta sinais de entrada e saida com o nó inoutCnt do elemento
                inoutPins_it = inoutPins.begin();
                while (inoutPins_it != inoutPins.end()) {
                    rt->addArc(tmp->inoutCnt, inoutPins_it->second, 0);
                    inoutPins_it++;
                }
            }
            
            if (tmp->linkP.type == GAP && eulerPathP_it->link != -1) { // nao é GAP na difusao P
                tmp->linkP = *eulerPathP_it;
                if (gapP) tmp->gapP = true;
                if (eulerPathP_it->type == SOURCE) {
                    rt->addNodetoNet(currentNetList.getTrans(eulerPathP_it->link).source, tmp->diffP);
                    if (lastElement->linkP.type != GAP && rt->getNet(lastElement->diffP) == rt->getNet(tmp->diffP) && currentNetList.getNet(currentNetList.getTrans(eulerPathP_it->link).source).trans.size() >= 2) //VERIFICAR POSTERIORMENTE ==2?
                        rt->addArc(tmp->diffP, lastElement->diffP, 0); //tira a ponte entre a difusao atual e ela mesma 
                } else {
                    rt->addNodetoNet(currentNetList.getTrans(eulerPathP_it->link).drain, tmp->diffP);
                    if (lastElement->linkP.type != GAP && rt->getNet(lastElement->diffP) == rt->getNet(tmp->diffP) && currentNetList.getNet(currentNetList.getTrans(eulerPathP_it->link).drain).trans.size() >= 2) //VERIFICAR POSTERIORMENTE ==2?
                        rt->addArc(tmp->diffP, lastElement->diffP, 0); //tira a ponte entre a difusao atual e ela mesma 
                }
                int x = diffPini[eulerPathP_it - currentNetList.getOrderingP().begin()];
                do {
                    if (trackPos[x] >= pDif_iniY) rt->addArc(tmp->met[x], tmp->diffP, COST_CNT_INSIDE_DIFF);
                    //cout << "aquip " << x << endl;
                } while (trackPos[++x] <= pDif_iniY + currentRules->getIntValue(currentNetList.getTrans(eulerPathP_it->link).width));
            }
            
            if (tmp->linkN.type == GAP && eulerPathN_it->link != -1) { // nao é GAP na difusao N
                tmp->linkN = *eulerPathN_it;
                if (gapN) tmp->gapN = true;
                if (eulerPathN_it->type == SOURCE) {
                    rt->addNodetoNet(currentNetList.getTrans(eulerPathN_it->link).source, tmp->diffN);
                    if (lastElement->linkN.type != GAP && rt->getNet(lastElement->diffN) == rt->getNet(tmp->diffN) && currentNetList.getNet(currentNetList.getTrans(eulerPathN_it->link).source).trans.size() >= 2) //VERIFICAR POSTERIORMENTE ==2?
                        rt->addArc(tmp->diffN, lastElement->diffN, 0); //tira a ponte entre a difusao atual e ela mesma
                } else {
                    rt->addNodetoNet(currentNetList.getTrans(eulerPathN_it->link).drain, tmp->diffN);
                    if (lastElement->linkN.type != GAP && rt->getNet(lastElement->diffN) == rt->getNet(tmp->diffN) && currentNetList.getNet(currentNetList.getTrans(eulerPathN_it->link).drain).trans.size() >= 2) //VERIFICAR POSTERIORMENTE ==2?
                        rt->addArc(tmp->diffN, lastElement->diffN, 0); //tira a ponte entre a difusao atual e ela mesma 
                }
                int x = diffNini[eulerPathN_it - currentNetList.getOrderingN().begin()];
                do {
                    if (trackPos[x] <= nDif_iniY) rt->addArc(tmp->met[x], tmp->diffN, COST_CNT_INSIDE_DIFF);
                    //cout << "aquin " << x << endl;
                } while (trackPos[--x] >= nDif_iniY - currentRules->getIntValue(currentNetList.getTrans(eulerPathN_it->link).width));
            }
            
            //GATE
            //desenha gate do transistor se nao for GAP
            lastElement = tmp;
            tmp = createElement(20, diffNini[eulerPathN_it - currentNetList.getOrderingN().begin()], diffPini[eulerPathP_it - currentNetList.getOrderingP().begin()]);
            
            //conecta aos pinos de entrada e saida
            for (int x = 0; x < trackPos.size(); x++) {
                if ((!decCostIOP && x == 0) || (!decCostION && x == trackPos.size() - 1)) rt->addArc(tmp->inoutCnt, tmp->met[x], 500);
                else rt->addArc(tmp->inoutCnt, tmp->met[x], 496);
            }
            
            if (eulerPathP_it->link != -1) { // nao é GAP na difusao P
                tmp->linkP = *eulerPathP_it;
                tmp->linkP.type = GATE;
                //Melhorar!!!
                for (int pos = diffPini[eulerPathP_it - currentNetList.getOrderingP().begin()]; trackPos[pos] - trackPos[diffPini[eulerPathP_it - currentNetList.getOrderingP().begin()]] <= currentRules->getIntValue(currentNetList.getTrans(eulerPathP_it->link).width); pos++)
                    rt->addNodetoNet(currentNetList.getTrans(eulerPathP_it->link).gate, tmp->pol[pos]);
                // VERIFICAR				rt->lockArc(tmp->pol[trackPos.size()-1], tmp->met[trackPos.size()-1], currentNetList.getTrans(eulerPathP_it->link).gate);
                //				if(pDif_iniY-currentRules->getIntValue(currentNetList.getTrans(eulerPathP_it->link).width)-currentRules->getRule(E1P1DF)-currentRules->getRule(S1P1P1)-currentRules->getRule(W2P1)-currentRules->getRule(S1DFP1)<0)
                //					rt->remArcs(tmp->outPolP);
                //				else
                //					rt->addArc(tmp->diffP,tmp->outPolP,5);
            } else tmp->linkP.type = GAP;
            
            inoutPins_it = inoutPins.begin();
            while (inoutPins_it != inoutPins.end()) {
                rt->addArc(tmp->inoutCnt, inoutPins_it->second, 0);
                inoutPins_it++;
            }
            
            if (eulerPathN_it->link != -1) { // nao é GAP na difusao N
                tmp->linkN = *eulerPathN_it;
                tmp->linkN.type = GATE;
                for (int pos = diffNini[eulerPathN_it - currentNetList.getOrderingN().begin()]; trackPos[diffNini[eulerPathN_it - currentNetList.getOrderingN().begin()]] - trackPos[pos] <= currentRules->getIntValue(currentNetList.getTrans(eulerPathN_it->link).width); pos--)
                    rt->addNodetoNet(currentNetList.getTrans(eulerPathN_it->link).gate, tmp->pol[pos]);
                // VERIFICAR			rt->lockArc(tmp->pol[0], tmp->met[0], currentNetList.getTrans(eulerPathN_it->link).gate);
                //				if(nDif_iniY+currentRules->getIntValue(currentNetList.getTrans(eulerPathN_it->link).width)+currentRules->getRule(E1P1DF)+currentRules->getRule(S1P1P1)+currentRules->getRule(W2P1)+currentRules->getRule(S1DFP1)>height)
                //					rt->remArcs(tmp->outPolN);
                //				else
                //					rt->addArc(tmp->diffN,tmp->outPolN,5);
            } else tmp->linkN.type = GAP;
            
            // DIFF
            lastElement = tmp;
            tmp = createElement(5, diffNini[eulerPathN_it - currentNetList.getOrderingN().begin()], diffPini[eulerPathP_it - currentNetList.getOrderingP().begin()]);
            
            if (eulerPathP_it->link != -1) { // nao é GAP na difusao P
                tmp->linkP = *eulerPathP_it;
                if (eulerPathP_it->type == DRAIN) {
                    tmp->linkP.type = SOURCE;
                    rt->addNodetoNet(currentNetList.getTrans(eulerPathP_it->link).source, tmp->diffP);
                } else {
                    tmp->linkP.type = DRAIN;
                    rt->addNodetoNet(currentNetList.getTrans(eulerPathP_it->link).drain, tmp->diffP);
                }
                int x = diffPini[eulerPathP_it - currentNetList.getOrderingP().begin()];
                do {
                    if (trackPos[x] >= pDif_iniY) rt->addArc(tmp->met[x], tmp->diffP, COST_CNT_INSIDE_DIFF);
                    //cout << "aquip " << x << endl;
                } while (trackPos[++x] <= pDif_iniY + currentRules->getIntValue(currentNetList.getTrans(eulerPathP_it->link).width));
            } else
                tmp->linkP.type = GAP;
            
            
            if (eulerPathN_it->link != -1) { // nao é GAP na difusao N
                tmp->linkN = *eulerPathN_it;
                if (eulerPathN_it->type == SOURCE) {
                    tmp->linkN.type = DRAIN;
                    rt->addNodetoNet(currentNetList.getTrans(eulerPathN_it->link).drain, tmp->diffN);
                } else {
                    tmp->linkN.type = SOURCE;
                    rt->addNodetoNet(currentNetList.getTrans(eulerPathN_it->link).source, tmp->diffN);
                }
                int x = diffNini[eulerPathN_it - currentNetList.getOrderingN().begin()];
                do {
                    if (trackPos[x] <= nDif_iniY) rt->addArc(tmp->met[x], tmp->diffN, COST_CNT_INSIDE_DIFF);
                    //cout << "aquin " << x << endl;
                } while (trackPos[--x] >= nDif_iniY - currentRules->getIntValue(currentNetList.getTrans(eulerPathN_it->link).width));
            } else tmp->linkN.type = GAP;
            
            rt->addArc(tmp->inoutCnt, tmp->met[center], 500);
            
            //conecta sinais de entrada e saida com o nó inoutCnt do elemento
            inoutPins_it = inoutPins.begin();
            while (inoutPins_it != inoutPins.end()) {
                rt->addArc(tmp->inoutCnt, inoutPins_it->second, 0);
                inoutPins_it++;
            }
            
            lastP_it = eulerPathP_it++;
            lastN_it = eulerPathN_it++;
        }
        
        //cria elemento lateral para roteamento
        tmp = createElement(20, diffNini[eulerPathN_it - currentNetList.getOrderingN().begin()], diffPini[eulerPathP_it - currentNetList.getOrderingP().begin()]); //MELHORAR!!!!!! max entre trans atual e próximo
        rt->addArc(tmp->inoutCnt, tmp->met[center], 550);
        
        //conecta sinais de entrada e saida com o nó inoutCnt do elemento
        inoutPins_it = inoutPins.begin();
        while (inoutPins_it != inoutPins.end()) {
            rt->addArc(tmp->inoutCnt, inoutPins_it->second, 0);
            inoutPins_it++;
        }
        state++;
        return true;
    } else
        return false;
}

bool AutoCell::route(int mCost, int pCost, int cCost, int ioCost) {
    cout << "Routing cell..." << endl;
    showIOCost();
    if (state < 4) return 0;
    state = 4;
    
    if (rt->routeNets(8000) && rt->optimize() && rt->optimize()) {
        /*		compaction cpt(CP_LP);
         cpt.setLPFilename("temp");
         list<Element>::iterator lastelements_it;
         int el=0;
         for(list<Element>::iterator elements_it=elements.begin(); elements_it!=elements.end(); elements_it++){
         if(elements_it!=elements.begin()){
         for(int x=0; x<trackPos.size();x++){
         cpt.insertUpperBound("E"+(el-1)+"_"+x+" + "+"E"+el+"_"+x,"M"+el);
         }
         cpt.insertLPMinVar("M"+el);
         }
         ++el;
         lastelements_it=elements_it;
         }
         */
        //        	rt->showResult();
        //		eletricalOtimization(currentCell,rt);
        state = 5;
    } else {
        cout << "Unable to route this circuit" << endl;
    }
    return state == 5;
}

bool AutoCell::compact(int mPriority, int pPriority, int gsPriority, int wPriority, string lpSolverFile) {
    cout << "Compacting layout..." << endl;
    if (state < 5) return 0;
    state = 5;
    
    currentRules = currentCircuit->getRules();
    
    vGrid = currentRules->getIntValue(currentCircuit->getVPitch());
    hGrid = currentRules->getIntValue(currentCircuit->getHPitch());
    height = currentCircuit->getRowHeight() * vGrid;
    
    currentLayout.clear();
    currentLayout.setName(currentCell->getName());
    
    map<string, int> IOgeometries;
    
    //    compaction cptY(CP_LP, "ILPy");
    compaction cpt(CP_LP, "ILPmodel");
    vector<Box*> geometries;
    
    cpt.insertConstraint("ZERO", "UM", CP_EQ, 1);
    cpt.insertConstraint("ZERO", "HGRID", CP_EQ, hGrid / 2);
    
    cpt.insertConstraint("ZERO", "height", CP_EQ, height);
    cpt.insertConstraint("ZERO", "yGNDb", CP_EQ, max(currentRules->getIntValue(currentCircuit->getSupplyVSize()), currentRules->getRule(W1M1)) / 2);
    cpt.insertConstraint("yVDDa", "height", CP_EQ, max(currentRules->getIntValue(currentCircuit->getSupplyVSize()), currentRules->getRule(W1M1)) / 2);
    cpt.insertConstraint("ZERO", "posNWell", CP_EQ, currentRules->getIntValue(1.14f)); //Improve!!
    cpt.insertConstraint("ZERO", "yNDiffa", CP_MIN, max(currentRules->getRule(E1INDF), currentRules->getRule(S1P1P1) / 2 + currentRules->getRule(E1P1DF)));
    cpt.insertConstraint("yPDiffb", "height", CP_MIN, max(currentRules->getRule(E1IPDF), currentRules->getRule(S1P1P1) / 2 + currentRules->getRule(E1P1DF)));
    cpt.insertConstraint("yNDiffb", "posNWell", CP_MIN, currentRules->getRule(E1WNDP));
    cpt.insertConstraint("posNWell", "yPDiffa", CP_MIN, currentRules->getRule(E1WNDP));
    //central track position
    cpt.insertConstraint("ZERO", "yCentralTrack", CP_EQ, center);
    
    
    
    /*  	cpt.insertConstraint( "yNDiffb", "yPolTrack[1]a", CP_MIN,  currentRules->getRule(E1P1DF));
     cpt.insertConstraint( "yPolTrack[1]a", "yPolTrack[1]b", CP_MIN,  currentRules->getRule(W2P1));
     //  	cpt.insertConstraint( "yPolTrack[1]a", "yPolTrack[1]b", CP_MAX,  1.1*currentRules->getRule(W1P1));
     cpt.insertConstraint( "yPolTrack[1]b", "yPDiffa", CP_MIN,  currentRules->getRule(E1P1DF));
     
     cpt.insertConstraint( "yPolCnt[1]a", "yPolTrack[1]a", CP_MIN,  0);
     cpt.insertConstraint( "yPolTrack[1]b", "yPolCnt[1]b", CP_MIN,  currentRules->getRule(E2P1CT));
     cpt.insertConstraint( "yPolMetCnt[1]a", "yPolMetCnt[1]b", CP_EQ,  currentRules->getRule(W2CT));
     cpt.insertConstraint( "yPolMetCnt[1]b", "yPolCnt[1]b", CP_MIN,  currentRules->getRule(E2P1CT));
     cpt.insertConstraint( "yPolyTrack[1]b", "yPDiffa", CP_MIN,  currentRules->getRule(E1P1DF));
     */
    
    
    list<Element>::iterator lastElements_it;
    vector<string> lastCntOrientPol(trackPos.size(), ""), lastCntOrientMet(trackPos.size(), ""), lastMetTrackX(trackPos.size(), ""), lastPolTrack(trackPos.size(), "");
    vector<string> currentMetTrack(trackPos.size(), ""), currentPolTrack(trackPos.size(), "");
    int x;
    
    //  Transistor Variables
    bool gapP, gapN;
    string lastNCntPos, lastPCntPos, lastDiffP, lastDiffN, outPolP, outPolN, diffTowerP, diffTowerN, lastNGatePos, lastPGatePos, limiteCntsP, limiteCntsN, difPos, cntPos, gatePos, compacDiffP, compacDiffN, pPolyCnt, nPolyCnt, pPolyGate, nPolyGate;
    vector<string> pCntPos(trackPos.size(), ""), nCntPos(trackPos.size(), ""), pDiffPos(trackPos.size(), ""), nDiffPos(trackPos.size(), "");
    
    //Compact the routing tracks
    for (list<Element>::iterator elements_it = elements.begin(); elements_it != elements.end(); ++elements_it) {
        elements_it->print();
        string lastPolTrackY = "", lastMetTrackY = "";
        currentMetTrack[0]="", currentMetTrack[trackPos.size() - 1]="";
        for (x = 0; x < trackPos.size(); x++) {
            //gera os metais para roteamento
            //conecta as trilhas de metal horizontalmente
            if (rt->getNet(elements_it->met[x]) != -1) {
                if (currentMetTrack[x] == "" || !rt->areConnected(elements_it->met[x], lastElements_it->met[x])) {
                    geometries.push_back(&currentLayout.addLayer(0, 0, 0, 0, MET1));
                    geometries.back()->setNet(currentNetList.getNetName(rt->getNet(elements_it->met[x])));
                    currentMetTrack[x] = intToStr(geometries.size() - 1);
                    cpt.insertConstraint("x" + currentMetTrack[x] + "a", "x" + currentMetTrack[x] + "b", CP_EQ, "x" + currentMetTrack[x] + "min");
                    cpt.insertLPMinVar("x" + currentMetTrack[x] + "min");
                    cpt.insertConstraint("y" + currentMetTrack[x] + "a", "y" + currentMetTrack[x] + "b", CP_EQ, currentRules->getRule(W1M1));
                    
                    cpt.insertConstraint("ZERO", "x" + currentMetTrack[x] + "a", CP_MIN, currentRules->getRule(S1M1M1) / 2);
                    cpt.insertConstraint("x" + currentMetTrack[x] + "b", "width", CP_MIN, currentRules->getRule(S1M1M1) / 2);
                    
                    //insere regras de distância mínima entre metais
                    if (lastMetTrackX[x] != "")
                        cpt.insertConstraint("x" + lastMetTrackX[x] + "b", "x" + currentMetTrack[x] + "a", CP_MIN, currentRules->getRule(S1M1M1));
                    if (currentNetList.getNetName(rt->getNet(elements_it->met[x])) != currentCircuit->getGndNet())
                        cpt.insertConstraint("yGNDb", "y" + currentMetTrack[x] + "a", CP_MIN, currentRules->getRule(W1M1));
                    if (currentNetList.getNetName(rt->getNet(elements_it->met[x])) != currentCircuit->getVddNet())
                        cpt.insertConstraint("y" + currentMetTrack[x] + "b", "yVDDa", CP_MIN, currentRules->getRule(W1M1));
                    
                    if (x == 0)
                        cpt.insertConstraint("y" + currentMetTrack[x] + "b", "yGNDb", CP_EQ, 0);
                    
                    if (x == trackPos.size() - 1)
                        cpt.insertConstraint("y" + currentMetTrack[x] + "a", "yVDDa", CP_EQ, 0);
                    
                }
                //insere regras de distância mínima entre metais
                if (lastMetTrackY != "" && !rt->areConnected(elements_it->met[x], elements_it->met[x - 1])) {
                    cpt.insertConstraint("y" + lastMetTrackY + "b", "y" + currentMetTrack[x] + "a", CP_MIN, currentRules->getRule(S1M1M1));
                }
                lastMetTrackY = currentMetTrack[x];
                
            }
            //connects to the adjacent metal1 track
            if (x >= 1 && rt->areConnected(elements_it->met[x], elements_it->met[x - 1])) {
                geometries.push_back(&currentLayout.addPolygon(0, 0, 0, 0, MET1));
                geometries.back()->setNet(currentNetList.getNetName(rt->getNet(elements_it->met[x])));
                string vMet = intToStr(geometries.size() - 1);
                cpt.insertConstraint("x" + vMet + "a", "x" + vMet + "b", CP_EQ, currentRules->getRule(W1M1));
                cpt.insertConstraint("x" + currentMetTrack[x] + "a", "x" + vMet + "a", CP_MIN, 0);
                cpt.insertConstraint("x" + vMet + "b", "x" + currentMetTrack[x] + "b", CP_MIN, 0);
                cpt.insertConstraint("x" + currentMetTrack[x - 1] + "a", "x" + vMet + "a", CP_MIN, 0);
                cpt.insertConstraint("x" + vMet + "b", "x" + currentMetTrack[x - 1] + "b", CP_MIN, 0);
                
                cpt.insertConstraint("y" + vMet + "a", "y" + vMet + "b", CP_EQ, "y" + vMet + "min"); // SECVAR????
                cpt.insertLPMinVar("y" + vMet + "min");
                cpt.insertConstraint("y" + vMet + "a", "y" + vMet + "b", CP_MIN, 0); // SECVAR????
                cpt.insertConstraint("y" + vMet + "a", "y" + currentMetTrack[x - 1] + "b", CP_EQ, 0);
                cpt.insertConstraint("y" + vMet + "b", "y" + currentMetTrack[x] + "a", CP_EQ, 0);
            }
            
            //conecta as trilhas de poly
            if (rt->getNet(elements_it->pol[x]) != -1) {
                if (currentPolTrack[x] == "" || !rt->areConnected(elements_it->pol[x], lastElements_it->pol[x])) {
                    geometries.push_back(&currentLayout.addLayer(0, 0, 0, 0, POLY));
                    currentPolTrack[x] = intToStr(geometries.size() - 1);
                    cpt.insertConstraint("x" + currentPolTrack[x] + "a", "x" + currentPolTrack[x] + "b", CP_EQ, "x" + currentPolTrack[x] + "min");
                    cpt.insertLPMinVar("x" + currentPolTrack[x] + "min", pPriority);
                    cpt.insertConstraint("ZERO", "x" + currentPolTrack[x] + "a", CP_MIN, currentRules->getRule(S1P1P1) / 2);
                    cpt.insertConstraint("y" + currentPolTrack[x] + "a", "y" + currentPolTrack[x] + "b", CP_EQ, currentRules->getRule(W2P1));
                    
                    //insere regras de distância mínima entre os polys
                    if (lastPolTrack[x] != "")
                        cpt.insertConstraint("x" + lastPolTrack[x] + "b", "x" + currentPolTrack[x] + "a", CP_MIN, currentRules->getRule(S1P1P1));
                }
                //insere regras de distância mínima entre metais
                if (lastPolTrackY != "" && !rt->areConnected(elements_it->pol[x], elements_it->pol[x - 1])) {
                    cpt.insertConstraint("y" + lastPolTrackY + "b", "y" + currentPolTrack[x] + "a", CP_MIN, currentRules->getRule(S1P1P1));
                }
                lastPolTrackY = currentPolTrack[x];
            }
            
            //roteamento vertical dos polys das trilhas
            if (x && rt->areConnected(elements_it->pol[x], elements_it->pol[x - 1])) { // && !(rt->isSource(elements_it->pol[x])&&rt->isSource(elements_it->pol[x-1]))
                
                geometries.push_back(&currentLayout.addPolygon(0, trackPos[x], 0, trackPos[x - 1], POLY));
                string vPol = intToStr(geometries.size() - 1);
                cpt.insertConstraint("x" + vPol + "a", "x" + vPol + "b", CP_EQ, currentRules->getRule(W2P1));
                cpt.insertConstraint("x" + currentPolTrack[x] + "a", "x" + vPol + "a", CP_MIN, 0);
                cpt.insertConstraint("x" + vPol + "b", "x" + currentPolTrack[x] + "b", CP_MIN, 0);
                cpt.insertConstraint("x" + currentPolTrack[x - 1] + "a", "x" + vPol + "a", CP_MIN, 0);
                cpt.insertConstraint("x" + vPol + "b", "x" + currentPolTrack[x - 1] + "b", CP_MIN, 0);
                
                cpt.insertConstraint("y" + vPol + "a", "y" + vPol + "b", CP_EQ, "y" + vPol + "min");
                cpt.insertLPMinVar("y" + vPol + "min",2);
                cpt.insertConstraint("y" + vPol + "a", "y" + vPol + "b", CP_MIN, 0);
                cpt.insertConstraint("y" + currentPolTrack[x - 1] + "a", "y" + vPol + "a", CP_EQ, 0);
                cpt.insertConstraint("y" + currentPolTrack[x] + "b", "y" + vPol + "b", CP_EQ, 0);
            }
            
            //conecta os polys com os metais das vias
            if (rt->areConnected(elements_it->met[x], elements_it->pol[x])) {
                string tmpCnt = insertCnt(geometries, cpt, elements_it, currentMetTrack, x);
                string tmpPol = insertCntPol(geometries, cpt, tmpCnt);
                
                cpt.insertConstraint("x" + currentPolTrack[x] + "a", "x" + tmpPol + "a", CP_MIN, 0);
                cpt.insertConstraint("x" + tmpPol + "b", "x" + currentPolTrack[x] + "b", CP_MIN, 0);
                cpt.insertConstraint("y" + tmpPol + "a", "y" + currentPolTrack[x] + "a", CP_MIN, 0);
                cpt.insertConstraint("y" + currentPolTrack[x] + "b", "y" + tmpPol + "b", CP_MIN, 0);
                
                if (x == 0 && pPolyGate != "") cpt.insertConstraint("x" + pPolyGate + "b", "x" + tmpPol + "a", CP_MIN, currentRules->getRule(S1P1P1));
                //				if(x==0 && !rt->areConnected(elements_it->ativeCntP,elements_it->pol[x])) pPolyCnt= tmpPol;
                if (x == trackPos.size() - 1 && nPolyGate != "") cpt.insertConstraint("x" + nPolyGate + "b", "x" + tmpPol + "a", CP_MIN, currentRules->getRule(S1P1P1));
                //				if(x==nrTracks-1 && !rt->areConnected(elements_it->ativeCntN,elements_it->interPol[x])) nPolyCnt= tmpPol;
                
            }
            /*
             //se for entrada/saida, alinha o metal1 com a grade
             if (rt->areConnected(elements_it->met[x], elements_it->inoutCnt)) {
             geometries.push_back(&currentLayout.addPolygon(0, trackPos[x]-(currentRules->getRule(W2VI) / 2) - currentRules->getRule(E1M1VI), 0, trackPos[x]+(currentRules->getRule(W2VI) / 2) + currentRules->getRule(E1M1VI), MET1));
             geometries.back()->setNet(currentNetList.getNetName(rt->getNet(elements_it->met[x])));
             string tmp = intToStr(geometries.size() - 1);
             cpt.insertConstraint("HGRID", "x" + tmp + "g", CP_EQ_VAR_VAL, "x" + tmp + "gpos", hGrid);
             cpt.forceIntegerVar("x" + tmp + "gpos");
             cpt.insertConstraint("x" + tmp + "a", "x" + tmp + "g", CP_EQ, currentRules->getRule(E1M1VI) + currentRules->getRule(W2VI) / 2);
             cpt.insertConstraint("x" + tmp + "g", "x" + tmp + "b", CP_EQ, currentRules->getRule(E1M1VI) + currentRules->getRule(W2VI) / 2);
             cpt.insertConstraint("x" + tmp + "g", "width", CP_MIN, hGrid / 2);
             cpt.insertConstraint("ZERO", "x" + tmp + "g", CP_MIN, hGrid / 2);
             
             cpt.insertConstraint("x" + currentMetTrack[x] + "a", "x" + tmp + "a", CP_MIN, 0);
             cpt.insertConstraint("x" + tmp + "b", "x" + currentMetTrack[x] + "b", CP_MIN, 0);
             
             IOgeometries[currentNetList.getNetName(rt->getNet(elements_it->inoutCnt))] = geometries.size() - 1;
             //				if(lastIO!="") cpt.insertConstraint("x" + lastIO + "b", "x" + tmp + "a", CP_MIN, 0);
             //				lastIO=tmp;
             }
             */
        }
        if (elements_it->gapP) gapP = true;
        if (elements_it->gapN) gapN = true;
        string tmp;
        
        switch (elements_it->linkN.type) {
            case GAP:
                gapN = true;
                break;
            case SOURCE:
            case DRAIN:
                for (x = center; x >= 0; x--) { // desenha contatos na difusao
                    if (rt->areConnected(elements_it->met[x], elements_it->diffN)) {
                        string cntPos=insertCnt(geometries, cpt, elements_it, currentMetTrack, x);
                        list<Element>::iterator next = elements_it; next++;
                        string difPos=insertCntDif(geometries, cpt, cntPos, currentMetTrack[x], lastNGatePos, lastDiffN, NDIF, next->gapN==true || next->linkN.type==GAP);
                        
                        /*
                         if (!gapN && lastNCntPos != cntPos) {
                         cpt.insertConstraint("x" + lastNCntPos + "b", "x" + cntPos + "a", CP_EQ, "x" + lastNCntPos + "_" + cntPos + "min");
                         cpt.insertLPMinVar("x" + lastNCntPos + "_" + cntPos + "min", gsPriority);
                         lastNCntPos = cntPos;
                         }
                         */
                        
                        nCntPos[x] = cntPos;
                        nDiffPos[x] = difPos;
                    }
                }
                lastNGatePos = "";
                gapN = false;
                break;
                
            case GATE:
                string gatePos = insertGate(geometries, cpt, elements_it->linkN.link, elements_it, currentPolTrack, nDiffPos, nCntPos, lastNGatePos, lastDiffN, NDIF);
                gapN = false;
                break;
        }
        switch (elements_it->linkP.type) {
            case GAP:
                gapP = true;
                break;
            case SOURCE:
            case DRAIN:
                for (x = center; x < elements_it->met.size(); ++x) { // desenha contatos na difusao
                    if (rt->areConnected(elements_it->met[x], elements_it->diffP)) {
                        string cntPos=insertCnt(geometries, cpt, elements_it, currentMetTrack, x);
                        list<Element>::iterator next = elements_it; next++;
                        string difPos=insertCntDif(geometries, cpt, cntPos, currentMetTrack[x], lastPGatePos, lastDiffP, PDIF, next->gapP==true || next->linkP.type==GAP);
                        
                        /*
                         if (!gapN && lastNCntPos != cntPos) {
                         cpt.insertConstraint("x" + lastNCntPos + "b", "x" + cntPos + "a", CP_EQ, "x" + lastNCntPos + "_" + cntPos + "min");
                         cpt.insertLPMinVar("x" + lastNCntPos + "_" + cntPos + "min", gsPriority);
                         lastNCntPos = cntPos;
                         }
                         */
                        pCntPos[x] = cntPos;
                        pDiffPos[x] = difPos;
                    }
                }
                lastPGatePos = "";
                gapP = false;
                break;
                
            case GATE:
                string gatePos = insertGate(geometries, cpt, elements_it->linkP.link, elements_it, currentPolTrack, pDiffPos, pCntPos, lastPGatePos, lastDiffP, PDIF);                
                gapP = false;
                break;
        }
        lastElements_it = elements_it;
        lastPolTrack = currentPolTrack;
        lastMetTrackX = currentMetTrack;
        
    }
    
    
    
    cpt.insertConstraint("ZERO", "width", CP_MIN, 0);
    cpt.insertConstraint("ZERO", "width", CP_EQ_VAR_VAL, "width_gpos", hGrid);
    cpt.forceIntegerVar("width_gpos");
    cpt.insertLPMinVar("width", wPriority);
    
    if (!cpt.solve(lpSolverFile)) {
        cout << "Unable to compact" << endl;
        return false;
    }
    
    for (int i = 0; i < geometries.size(); i++) {
        
        int xa = cpt.getVariableVal("x" + intToStr(i) + "a");
        int xb = cpt.getVariableVal("x" + intToStr(i) + "b");
        int ya = cpt.getVariableVal("y" + intToStr(i) + "a");
        int yb = cpt.getVariableVal("y" + intToStr(i) + "b");
        
        if (xa != -1 && xb != -1) {
            geometries[i]->setWidth(xb - xa);
            geometries[i]->setX((xb + xa) / 2);
        }
        if (ya != -1 && yb != -1) {
            geometries[i]->setHeight(yb - ya);
            geometries[i]->setY((yb + ya) / 2);
        }
        
    }
    int width = cpt.getVariableVal("width");
    /*
     //Aqui duplica as camadas de M1 relacionadas aos pinos de I/O
     list <Box>::iterator net_it;
     for ( net_it = currentLayout.layers[MET1].begin(); net_it != currentLayout.layers[MET1].end(); net_it++ )
     if(currentNetList.isIO(net_it->getNet())) currentLayout.addEnc(*net_it,  0 , MET1P); 
	 
     for (map<string,int>::iterator IOgeometries_it=IOgeometries.begin(); IOgeometries_it != IOgeometries.end(); IOgeometries_it++ ) {
     Pin p;
     p.setX(geometries[IOgeometries_it->second]->getX());
     p.setY(geometries[IOgeometries_it->second]->getY());
     p.setLayer(MET1P);
     currentLayout.setPin(IOgeometries_it->first,p);
     currentLayout.addLabel(IOgeometries_it->first,p);
     }
	 
     list <Box>::iterator layer_it;
     for ( layer_it = currentLayout.layers[PDIF].begin(); layer_it != currentLayout.layers[PDIF].end(); layer_it++ )
     &currentLayout.addEnc(*layer_it,  currentRules->getRule(E1IPDF) , PSEL);
     for ( layer_it = currentLayout.layers[NDIF].begin(); layer_it != currentLayout.layers[NDIF].end(); layer_it++ )
     &currentLayout.addEnc(*layer_it,  currentRules->getRule(E1INDF) , NSEL);
	 
     bool btP=false, btN=false;
     list<int> btIntervalsP,btIntervalsN;
     btIntervalsP.push_back(currentRules->getIntValue(0.45));
     btIntervalsP.push_back(width-currentRules->getIntValue(0.45));
     btIntervalsN=btIntervalsP;
     for ( layer_it = currentLayout.layers[PDIF].begin(); layer_it != currentLayout.layers[PDIF].end(); layer_it++ ){
     if(layer_it->getY1() < currentRules->getRule(S1CTCT)/2+currentRules->getRule(W2CT)+currentRules->getRule(E1DFCT)+currentRules->getRule(S1DFDF)){
     for(list<int>::iterator btIntervals_it=btIntervalsP.begin(); btIntervals_it!=btIntervalsP.end(); ++btIntervals_it){
     list<int>::iterator ini, end;
     ini=btIntervals_it;
     end=++btIntervals_it;
     if(layer_it->getX1()>*ini && layer_it->getX2()<*end){
     btIntervalsP.insert(end, layer_it->getX1());
     btIntervalsP.insert(end, layer_it->getX2());
     }
     else if(layer_it->getX1()<*ini && layer_it->getX2()>*end){
     btIntervalsP.erase(ini);
     btIntervalsP.erase(end);
     }				
     else if(layer_it->getX1()<*ini && layer_it->getX2()>*ini) 
     *ini=layer_it->getX2();
     else if(layer_it->getX1()<*end && layer_it->getX2()>*end) 
     *end=layer_it->getX1();
     }
     }
     }
     for(list<int>::iterator btIntervals_it=btIntervalsP.begin(); btIntervals_it!=btIntervalsP.end(); ++btIntervals_it){
     list<int>::iterator ini, end;
     ini=btIntervals_it;
     end=++btIntervals_it;
     if(*ini+currentRules->getRule(W2CT)+2*currentRules->getRule(E1DFCT)+2*currentRules->getRule(S1DFDF)<=*end){
     btP=true;
     currentLayout.addPolygon(*ini+currentRules->getRule(S1DFDF)-currentRules->getRule(E1INDF), 0, *end-currentRules->getRule(S1DFDF)+currentRules->getRule(E1INDF), currentRules->getRule(S1CTCT)/2+currentRules->getRule(W2CT)+currentRules->getRule(E1DFCT)+currentRules->getRule(E1INDF), NSEL);
     currentLayout.addPolygon(*ini+currentRules->getRule(S1DFDF), 0, *end-currentRules->getRule(S1DFDF), currentRules->getRule(S1CTCT)/2+currentRules->getRule(W2CT)+currentRules->getRule(E1DFCT), NDIF);
     for(int cnt=*ini+currentRules->getRule(S1DFDF)+currentRules->getRule(E1DFCT); cnt<=*end-currentRules->getRule(S1DFDF)-currentRules->getRule(E1DFCT)-currentRules->getRule(W2CT); cnt+=currentRules->getRule(W2CT)+currentRules->getRule(S1CTCT))
     currentLayout.addPolygon(cnt, currentRules->getRule(S1CTCT)/2, cnt+currentRules->getRule(W2CT), currentRules->getRule(S1CTCT)/2+currentRules->getRule(W2CT), CONT);				
     }
     }
	 
     for ( layer_it = currentLayout.layers[NDIF].begin(); layer_it != currentLayout.layers[NDIF].end(); layer_it++ ){
     if(layer_it->getY2() > height-(currentRules->getRule(S1CTCT)/2+currentRules->getRule(W2CT)+currentRules->getRule(E1DFCT)+currentRules->getRule(S1DFDF))){
     for(list<int>::iterator btIntervals_it=btIntervalsN.begin(); btIntervals_it!=btIntervalsN.end(); ++btIntervals_it){
     list<int>::iterator ini, end;
     ini=btIntervals_it;
     end=++btIntervals_it;
     if(layer_it->getX1()>*ini && layer_it->getX2()<*end){
     btIntervalsN.insert(end, layer_it->getX1());
     btIntervalsN.insert(end, layer_it->getX2());
     }
     else if(layer_it->getX1()<*ini && layer_it->getX2()>*end){
     btIntervalsN.erase(ini);
     btIntervalsN.erase(end);
     }				
     else if(layer_it->getX1()<*ini && layer_it->getX2()>*ini) 
     *ini=layer_it->getX2();
     else if(layer_it->getX1()<*end && layer_it->getX2()>*end) 
     *end=layer_it->getX1();
     }
     }
     }
     for(list<int>::iterator btIntervals_it=btIntervalsN.begin(); btIntervals_it!=btIntervalsN.end(); ++btIntervals_it){
     list<int>::iterator ini, end;
     ini=btIntervals_it;
     end=++btIntervals_it;
     if(*ini+currentRules->getRule(W2CT)+2*currentRules->getRule(E1DFCT)+2*currentRules->getRule(S1DFDF)<=*end){
     btN=true;
     currentLayout.addPolygon(*ini+currentRules->getRule(S1DFDF)-currentRules->getRule(E1INDF), height-(currentRules->getRule(S1CTCT)/2+currentRules->getRule(W2CT)+currentRules->getRule(E1DFCT)+currentRules->getRule(E1INDF)), *end-currentRules->getRule(S1DFDF)+currentRules->getRule(E1INDF), height, PSEL);
     currentLayout.addPolygon(*ini+currentRules->getRule(S1DFDF), height-(currentRules->getRule(S1CTCT)/2+currentRules->getRule(W2CT)+currentRules->getRule(E1DFCT)), *end-currentRules->getRule(S1DFDF), height, PDIF);
     for(int cnt=*ini+currentRules->getRule(S1DFDF)+currentRules->getRule(E1DFCT); cnt<=*end-currentRules->getRule(S1DFDF)-currentRules->getRule(E1DFCT)-currentRules->getRule(W2CT); cnt+=currentRules->getRule(W2CT)+currentRules->getRule(S1CTCT))
     currentLayout.addPolygon(cnt, height-(currentRules->getRule(S1CTCT)/2+currentRules->getRule(W2CT)), cnt+currentRules->getRule(W2CT), height-currentRules->getRule(S1CTCT)/2, CONT);				
     }
     }
	 
     if(!btP) cout << "Could not insert bodye ties to the P transistors" << endl;
     if(!btN) cout << "Could not insert bodye ties to the N transistors" << endl;
     */
    currentLayout.setWidth(width);
    currentLayout.setHeight(height);
    
    //Draw supply strips
    currentLayout.addPolygon(0, currentRules->getRule(S1CTCT) / 2 + currentRules->getRule(W2CT) + currentRules->getRule(E1DFCT) + currentRules->getRule(S1DFDF) - currentRules->getRule(E1INDF), width, cpt.getVariableVal("posNWell"), NSEL);
    currentLayout.addPolygon(0, height - (currentRules->getRule(S1CTCT) / 2 + currentRules->getRule(W2CT) + currentRules->getRule(E1DFCT) + currentRules->getRule(S1DFDF) - currentRules->getRule(E1INDF)), width, cpt.getVariableVal("posNWell"), PSEL);
    
    currentLayout.addPolygon(0, 0, width, supWidth, MET1).setNet(currentCircuit->getVddNet());
    currentLayout.addPolygon(0, 0, width, supWidth, MET1P);
    currentLayout.addPolygon(0, height - supWidth, width, height, MET1).setNet(currentCircuit->getGndNet());
    currentLayout.addPolygon(0, height - supWidth, width, height, MET1P);
    currentLayout.addPolygon(0, 0, width, height, CELLBOX);
    currentLayout.addPolygon(currentRules->getRule(S1DFDF) / 2 - currentRules->getRule(E1WNDP), height + currentRules->getRule(S1DNWN), width - currentRules->getRule(S1DFDF) / 2 + currentRules->getRule(E1WNDP), cpt.getVariableVal("posNWell"), NWEL);
    
    
    //	currentLayout.merge();
    currentCircuit->insertLayout(currentLayout);
    cout << "Cell Size (W x H): " << float(currentLayout.getWidth()) / currentRules->getScale() << " x " << float(currentLayout.getHeight()) / currentRules->getScale() << endl;
    state = 6;
    return state == 6;
}


string AutoCell::insertGate(vector<Box*> &geometries, compaction &cpt, int transistor, list<Element>::iterator elements_it, vector<string> &currentPolTrack, vector<string> &difsPos, vector<string> &cntsPos, string &lastGatePos, string currentDiff, layer_name l){
    int transWidth = int(ceil((currentNetList.getTrans(transistor).width * currentRules->getScale()) / 2)*2);
    int transLength = int(ceil((currentNetList.getTrans(transistor).length * currentRules->getScale()) / 2)*2);
    if (transLength < currentRules->getRule(W2P1)) cout << "WARNING: Gate length of transistor " << currentNetList.getTrans(transistor).name << " is smaller than the minimum of the technology" << endl;
    
    //draw gate
    geometries.push_back(&currentLayout.addPolygon(0, 0, 0, 0, POLY));
    string gatePos = intToStr(geometries.size() - 1);
    
    cpt.insertConstraint("x" + gatePos + "a", "x" + gatePos + "b", CP_EQ, transLength);
    cpt.insertConstraint("x" + currentDiff + "a", "x" + gatePos + "a", CP_MIN, currentRules->getRule(E1DFP1));
    cpt.insertConstraint("x" + gatePos + "b", "x" + currentDiff + "b", CP_MIN, currentRules->getRule(E1DFP1));
    cpt.insertConstraint("x" + currentDiff + "a", "x" + currentDiff + "b", CP_EQ, "x" + currentDiff + "min");
    cpt.insertLPMinVar("x" + currentDiff + "min");
    
    cpt.insertConstraint("y" + currentDiff + "a", "y" + currentDiff + "b", CP_EQ, transWidth);
    cpt.insertConstraint("y" + gatePos + "a", "y" + currentDiff + "a", CP_EQ, currentRules->getRule(E1P1DF));
    cpt.insertConstraint("y" + currentDiff + "b", "y" + gatePos + "b", CP_EQ, currentRules->getRule(E1P1DF));
    cpt.insertConstraint("ZERO", "b" + currentDiff + "_smallTransWidth", CP_EQ, (transWidth<currentRules->getRule(S3DFP1)?1:0));    
    
    if (lastGatePos != "")
        cpt.insertConstraint("x" + lastGatePos + "b", "x" + gatePos + "a", CP_MIN, currentRules->getRule(S1P1P1));
            
    if (l==NDIF){
        cpt.insertConstraint("y" + currentDiff + "b", "yNDiffb", CP_MIN, 0);
        for (int x = center; x > 0; --x){ // align gate to the internal tracks
            if (rt->isSource(elements_it->pol[x])) {
                cpt.insertConstraint("x" + currentPolTrack[x] + "a", "x" + gatePos + "a", CP_EQ, 0);
                cpt.insertConstraint("x" + currentPolTrack[x] + "b", "x" + gatePos + "b", CP_EQ, 0);
                cpt.insertConstraint("y" + currentDiff + "b", "y" + currentPolTrack[x + 1] + "a", CP_MIN, currentRules->getRule(E1P1DF));
                cpt.insertConstraint("y" + gatePos + "b", "y" + currentPolTrack[x] + "a", CP_EQ, 0);   
            }
            if (difsPos[x] != "") {
                cpt.insertConstraint("y" + difsPos[x] + "b", "y" + currentDiff + "b", CP_MIN, 0);
                cpt.insertConstraint("y" + difsPos[x] + "a", "y" + currentDiff + "b", CP_EQ, "y" + currentDiff + "min");
                cpt.insertLPMinVar("y" + currentDiff + "min");
                
                //                        if (currentRules->getRule(S2DFP1) && transWidthN < currentRules->getRule(S3DFP1))
                
                //insert conditional diff to gate rule in L shape transistors considering S3DFP1 (big transistor width)
                cpt.forceBinaryVar("b" + difsPos[x] + "_LshapeBeforeGate");
                cpt.insertConstraint("x" + difsPos[x] + "b", "x" + gatePos + "a", CP_MIN, "b" + difsPos[x] + "_LshapeBeforeGate", currentRules->getRule(S1DFP1));
                cpt.insertConstraint("ZERO", "y" + difsPos[x] + "LdistBeforeGate", CP_MAX, "b" + difsPos[x] + "_LshapeBeforeGate", 100000);             
                cpt.insertConstraint("UM", "b" + currentDiff + "_smallTransWidth" + " + " + "b" + difsPos[x] + "_LshapeBeforeGate", CP_MAX, "b" + difsPos[x] + "_applyS2BeforeGate");
                cpt.insertConstraint("x" + difsPos[x] + "b", "x" + gatePos + "a", CP_MIN, "b" + difsPos[x] + "_applyS2BeforeGate", currentRules->getRule(S2DFP1));

                
                //                        else
                //                            cpt.insertConstraint("x" + nDiffPos[x] + "b", "x" + gatePos + "a", CP_MIN, currentRules->getRule(S1DFP1));
                cpt.insertConstraint("x" + cntsPos[x] + "b", "x" + gatePos + "a", CP_MIN, currentRules->getRule(S1CTP1));
                cpt.insertConstraint("x" + cntsPos[x] + "b", "x" + gatePos + "a", CP_EQ, "x" + gatePos + "min");
                cpt.insertLPMinVar("x" + gatePos + "min");
                difsPos[x] = "";
            }
            //                    if (trackPos[x] + currentRules->getRule(W2CT) + currentRules->getRule(E1DFCT) >= nDif_iniY - transWidthP)
        }
        
    }else{
        cpt.insertConstraint("yPDiffa", "y" + currentDiff + "a", CP_MIN, 0);
        for (int x = center; x < elements_it->pol.size(); ++x){ // align gate to the internal tracks
            if (rt->isSource(elements_it->pol[x])) {
                cpt.insertConstraint("x" + currentPolTrack[x] + "a", "x" + gatePos + "a", CP_EQ, 0);
                cpt.insertConstraint("x" + currentPolTrack[x] + "b", "x" + gatePos + "b", CP_EQ, 0);
                cpt.insertConstraint("y" + currentPolTrack[x - 1] + "b", "y" + currentDiff + "a", CP_MIN, currentRules->getRule(E1P1DF));
                cpt.insertConstraint("y" + gatePos + "a", "y" + currentPolTrack[x] + "b", CP_EQ, 0);
            }    
            if (difsPos[x] != "") {
                cpt.insertConstraint("y" + currentDiff + "a", "y" + difsPos[x] + "a", CP_MIN, 0);
                cpt.insertConstraint("y" + currentDiff + "a", "y" + difsPos[x] + "b", CP_EQ, "y" + currentDiff + "min");
                cpt.insertLPMinVar("y" + currentDiff + "min");
                
                //                        if (currentRules->getRule(S2DFP1) && transWidthN < currentRules->getRule(S3DFP1))
                
                // REPETIDO... REFATORAR ABAIXO 
                
                //insert conditional diff to gate rule in L shape transistors considering S3DFP1 (big transistor width)
                cpt.forceBinaryVar("b" + difsPos[x] + "_LshapeBeforeGate");
                cpt.insertConstraint("x" + difsPos[x] + "b", "x" + gatePos + "a", CP_MIN, "b" + difsPos[x] + "_LshapeBeforeGate", currentRules->getRule(S1DFP1));
                cpt.insertConstraint("ZERO", "y" + difsPos[x] + "LdistBeforeGate", CP_MAX, "b" + difsPos[x] + "_LshapeBeforeGate", 100000);             
                cpt.insertConstraint("UM", "b" + currentDiff + "_smallTransWidth" + " + " + "b" + difsPos[x] + "_LshapeBeforeGate", CP_MAX, "b" + difsPos[x] + "_applyS2BeforeGate");
                cpt.insertConstraint("x" + difsPos[x] + "b", "x" + gatePos + "a", CP_MIN, "b" + difsPos[x] + "_applyS2BeforeGate", currentRules->getRule(S2DFP1));
                //                        else
                //                            cpt.insertConstraint("x" + nDiffPos[x] + "b", "x" + gatePos + "a", CP_MIN, currentRules->getRule(S1DFP1));
                cpt.insertConstraint("x" + cntsPos[x] + "b", "x" + gatePos + "a", CP_MIN, currentRules->getRule(S1CTP1));
                cpt.insertConstraint("x" + cntsPos[x] + "b", "x" + gatePos + "a", CP_EQ, "x" + gatePos + "min");
                cpt.insertLPMinVar("x" + gatePos + "min");
                difsPos[x] = "";
            }
            //                    if (trackPos[x] + currentRules->getRule(W2CT) + currentRules->getRule(E1DFCT) >= nDif_iniY - transWidthP)
        }
    }
    
    //if there is no contact between 2 gates and they have different widths
    /*                if (rt->areConnected(elements_it->diffN, elements_it->pol[trackPos.size() - 1])) {
     if (rt->getNrFinalArcs(elements_it->pol[trackPos.size() - 1]) > 1) {
     geometries.push_back(&currentLayout.addPolygon(0, nDif_iniY, 0, trackPos[trackPos.size() - 1], POLY));
     string ponte = intToStr(geometries.size() - 1);
     
     cpt.insertConstraint("x" + ponte + "a", "x" + ponte + "b", CP_EQ, currentRules->getRule(W2P1));
     cpt.insertConstraint("x" + gatePos + "a", "x" + ponte + "a", CP_MIN, 0);
     cpt.insertConstraint("x" + ponte + "b", "x" + gatePos + "b", CP_MIN, 0);
     cpt.insertConstraint("x" + currentPolTrack[trackPos.size() - 1] + "a", "x" + ponte + "a", CP_MIN, 0);
     cpt.insertConstraint("x" + ponte + "b", "x" + currentPolTrack[trackPos.size() - 1] + "b", CP_MIN, 0);
     }
     }
     */
    lastGatePos = gatePos;
    return gatePos;
}

string AutoCell::insertCntDif(vector<Box*> &geometries, compaction &cpt, string cntPos, string metTrack, string lastGatePos, string &lastDiff, layer_name l, bool endDiff) {
    geometries.push_back(&currentLayout.addEnc(*geometries.back(), 0, l));
    string diffEnc = intToStr(geometries.size() - 1);
    
    //diffusion enclosure of contact - 3 sides E1DFCT, one side E2DFCT  (comentar que ele coloca o lado menor em cima ou embaixo tbm)
    cpt.forceBinaryVar("b" + diffEnc + "_l");
    cpt.forceBinaryVar("b" + diffEnc + "_r");
    cpt.forceBinaryVar("b" + diffEnc + "_b");
    cpt.forceBinaryVar("b" + diffEnc + "_t");
    cpt.insertConstraint("ZERO", "b" + diffEnc + "_l + b" + diffEnc + "_r + b" + diffEnc + "_b + b" + diffEnc + "_t", CP_EQ, 3);
    cpt.insertConstraint("x" + diffEnc + "a", "x" + cntPos + "a", CP_MIN, currentRules->getRule(E2DFCT));
    cpt.insertConstraint("x" + diffEnc + "a", "x" + cntPos + "a", CP_MIN, "b" + diffEnc + "_l", currentRules->getRule(E1DFCT));
    cpt.insertConstraint("x" + cntPos + "b", "x" + diffEnc + "b", CP_MIN, currentRules->getRule(E2DFCT));
    cpt.insertConstraint("x" + cntPos + "b", "x" + diffEnc + "b", CP_MIN, "b" + diffEnc + "_r", currentRules->getRule(E1DFCT));
    cpt.insertConstraint("y" + diffEnc + "a", "y" + cntPos + "a", CP_MIN, currentRules->getRule(E2DFCT));
    cpt.insertConstraint("y" + diffEnc + "a", "y" + cntPos + "a", CP_MIN, "b" + diffEnc + "_b", currentRules->getRule(E1DFCT));
    cpt.insertConstraint("y" + cntPos + "b", "y" + diffEnc + "b", CP_MIN, currentRules->getRule(E2DFCT));
    cpt.insertConstraint("y" + cntPos + "b", "y" + diffEnc + "b", CP_MIN, "b" + diffEnc + "_t", currentRules->getRule(E1DFCT));
    
    //if there is a gate before
    if (lastGatePos != "") {
        cpt.insertConstraint("x" + lastGatePos + "b", "x" + cntPos + "a", CP_MIN, currentRules->getRule(S1CTP1));
        cpt.insertConstraint("x" + lastGatePos + "b", "x" + cntPos + "a", CP_EQ, "x" + cntPos + "min");
        cpt.insertLPMinVar("x" + cntPos + "min");
        cpt.insertConstraint("x" + lastDiff + "b", "width", CP_MIN, currentRules->getRule(S1DFDF) / 2);
        cpt.insertConstraint("x" + diffEnc + "b", "x" + lastDiff + "b", CP_EQ, 0);

        //insert conditional diff to gate rule in L shape transistors considering S3DFP1 (big transistor width)
        cpt.forceBinaryVar("b" + diffEnc + "_LshapeAfterGate");
        cpt.insertConstraint("x" + lastGatePos + "b", "x" + diffEnc + "a", CP_MIN, "b" + diffEnc + "_LshapeAfterGate", currentRules->getRule(S1DFP1));
        cpt.insertConstraint("ZERO", "y" + diffEnc + "LdistAfterGate", CP_MAX, "b" + diffEnc + "_LshapeAfterGate", 100000);             
        cpt.insertConstraint("UM", "b" + lastDiff + "_smallTransWidth" + " + " + "b" + diffEnc + "_LshapeAfterGate", CP_MAX, "b" + diffEnc + "_applyS2AfterGate");
        cpt.insertConstraint("x" + lastGatePos + "b", "x" + diffEnc + "a", CP_MIN, "b" + diffEnc + "_applyS2AfterGate", currentRules->getRule(S2DFP1));

        //diffusion extension rules for last gate diffusion
        if (l==NDIF) {
            cpt.insertConstraint("y" + diffEnc + "b", "y" + lastDiff + "b", CP_MIN, 0);
            cpt.insertConstraint("y" + diffEnc + "LdistAfterGate", "y" + lastDiff + "a", CP_MAX, "y" + diffEnc + "a");            
        }else{
            cpt.insertConstraint("y" + lastDiff + "a", "y" + diffEnc + "a", CP_MIN, 0);
            cpt.insertConstraint("y" + diffEnc + "LdistAfterGate", "y" + diffEnc + "b", CP_MAX, "y" + lastDiff + "b");            
        }
        cpt.insertLPMinVar("y" + diffEnc + "LdistAfterGate",2);
    }
    
    //if there is not a gap after
    if(!endDiff){
        //create next diffusion
        geometries.push_back(&currentLayout.addPolygon(0, 0, 0, 0, l));
        string currentDiff = intToStr(geometries.size() - 1);

        cpt.insertConstraint("x" + currentDiff + "a", "x" + currentDiff + "b", CP_EQ, "x" + currentDiff + "min");
        cpt.insertLPMinVar("x" + currentDiff + "min",4);
        cpt.insertConstraint("ZERO", "x" + currentDiff + "a", CP_MIN, currentRules->getRule(S1DFDF) / 2);
        cpt.insertConstraint("x" + currentDiff + "a", "x" + diffEnc + "a", CP_EQ, 0);

        //merge new diffusion to the last one and maximize intersection
        if (lastGatePos != "") {
            cpt.insertConstraint("x" + currentDiff + "a", "x" + lastDiff + "b", CP_MIN, 0);
            cpt.insertConstraint("y" + currentDiff + "b_int", "y" + lastDiff + "b", CP_MIN, 0);
            cpt.insertConstraint("y" + currentDiff + "b_int", "y" + currentDiff + "b", CP_MIN, 0);
            cpt.insertConstraint("y" + lastDiff + "a", "y" + currentDiff + "a_int", CP_MIN, 0);
            cpt.insertConstraint("y" + currentDiff + "a", "y" + currentDiff + "a_int", CP_MIN, 0);
            cpt.insertConstraint( "y" + currentDiff + "a_int", "y" + currentDiff + "b_int", CP_EQ,  "y" + currentDiff + "a_int_min");
            cpt.insertLPMinVar("y" + currentDiff + "a_int_min",-4);
        }else{
            //insert GAP between diffusions
            cpt.insertConstraint("x" + lastDiff + "b", "x" + currentDiff + "a", CP_MIN, currentRules->getRule(S1DFDF));
        }
        
        //diffusion extension rules for next gate diffusion
        if (l==NDIF) {
            cpt.insertConstraint("y" + diffEnc + "b", "y" + currentDiff + "b", CP_MIN, 0);
            cpt.insertConstraint("y" + diffEnc + "LdistBeforeGate", "y" + currentDiff + "a", CP_MAX, "y" + diffEnc + "a");            
        }else{
            cpt.insertConstraint("y" + currentDiff + "a", "y" + diffEnc + "a", CP_MIN, 0);
            cpt.insertConstraint("y" + diffEnc + "LdistBeforeGate", "y" + diffEnc + "b", CP_MAX, "y" + currentDiff + "b");            
        }
        cpt.insertLPMinVar("y" + diffEnc + "LdistBeforeGate",2);
        lastDiff = currentDiff;
    }
    
    return diffEnc;
}

string AutoCell::insertCnt(vector<Box*> &geometries, compaction &cpt, list<Element>::iterator elements_it, vector<string> metTracks, int pos) {
    geometries.push_back(&currentLayout.addLayer(0, 0, 0, 0, CONT));
    string cntPos = intToStr(geometries.size() - 1);
    
    geometries.push_back(&currentLayout.addLayer(0, 0, 0, 0, MET1));
    string metPos = intToStr(geometries.size() - 1);
    
    cpt.insertConstraint("x" + cntPos + "a", "x" + cntPos + "b", CP_EQ, currentRules->getRule(W2CT));
    cpt.insertConstraint("y" + cntPos + "a", "y" + cntPos + "b", CP_EQ, currentRules->getRule(W2CT));
    
    //metal head over contact
    cpt.forceBinaryVar("b" + cntPos + "_1M");
    cpt.forceBinaryVar("b" + cntPos + "_2M");
    cpt.forceBinaryVar("b" + cntPos + "_3M");
    cpt.insertConstraint("ZERO", "b" + cntPos + "_1M" + " + " + "b" + cntPos + "_2M"  + " + " + "b" + cntPos + "_3M", CP_EQ, 1);
    
    cpt.insertConstraint("ZERO", "x" + cntPos + "hM", CP_MIN, "b" + cntPos + "_1M", currentRules->getRule(E2M1CT));
    cpt.insertConstraint("ZERO", "x" + cntPos + "hM", CP_MIN, "b" + cntPos + "_2M", currentRules->getRule(E3M1CT));
    cpt.insertConstraint("ZERO", "x" + cntPos + "hM", CP_MIN, "b" + cntPos + "_3M", currentRules->getRule(E1M1CT));
    cpt.insertConstraint("x" + metPos + "a", "x" + cntPos + "a", CP_MIN, "x" + cntPos + "hM");
    cpt.insertConstraint("x" + cntPos + "b", "x" + metPos + "b", CP_MIN, "x" + cntPos + "hM");
    cpt.insertConstraint("x" + metPos + "a", "x" + metPos + "b", CP_EQ, "x" + cntPos + "hminM");
    cpt.insertLPMinVar("x" + cntPos + "hminM",3);
    
    cpt.insertConstraint("ZERO", "y" + cntPos + "vM", CP_MIN, "b" + cntPos + "_1M", currentRules->getRule(E1M1CT));
    cpt.insertConstraint("ZERO", "y" + cntPos + "vM", CP_MIN, "b" + cntPos + "_2M", currentRules->getRule(E3M1CT));
    cpt.insertConstraint("ZERO", "y" + cntPos + "vM", CP_MIN, "b" + cntPos + "_3M", currentRules->getRule(E2M1CT));
    cpt.insertConstraint("y" + metPos + "a", "y" + cntPos + "a", CP_MIN, "y" + cntPos + "vM");
    cpt.insertConstraint("y" + cntPos + "b", "y" + metPos + "b", CP_MIN, "y" + cntPos + "vM");
    cpt.insertConstraint("y" + metPos + "a", "y" + metPos + "b", CP_EQ, "y" + cntPos + "vminM");
    cpt.insertLPMinVar("y" + cntPos + "vminM",3);
    
    
    //create intersection with the tracks
    cpt.insertConstraint("y" + metTracks[pos] + "a", "y" + metPos + "b", CP_MIN, currentRules->getRule(W1M1));
    cpt.insertConstraint("y" + metPos + "a", "y" + metTracks[pos] + "b", CP_MIN, currentRules->getRule(W1M1));
    cpt.insertConstraint("x" + metTracks[pos] + "a", "x" + metPos + "b", CP_MIN, currentRules->getRule(W1M1));
    cpt.insertConstraint("x" + metPos + "a", "x" + metTracks[pos] + "b", CP_MIN, currentRules->getRule(W1M1));
    
    //space rule to the adjacent tracks
//    if (!rt->areConnected(elements_it->met[pos], elements_it->met[pos - 1]))
//        cpt.insertConstraint("y" + metTracks[pos-1] + "b", "y" + metPos + "a", CP_MIN, currentRules->getRule(S1M1M1));
//    if (!rt->areConnected(elements_it->met[pos], elements_it->met[pos + 1]))
//        cpt.insertConstraint("y" + metPos + "b", "y" + metTracks[pos+1] + "a", CP_MIN, currentRules->getRule(S1M1M1));
//    cpt.insertConstraint("x" + metTracks[pos] + "a", "x" + metPos + "b", CP_MIN, currentRules->getRule(S1M1M1));
//    cpt.insertConstraint("x" + metPos + "a", "x" + metTracks[pos] + "b", CP_MIN, currentRules->getRule(S1M1M1));
    
    return cntPos;
}

string AutoCell::insertCntPol(vector<Box*> &geometries, compaction &cpt, string cntPos) {
    geometries.push_back(&currentLayout.addEnc(*geometries.back(), 0, POLY));
    string polPos = intToStr(geometries.size() - 1);
    
    //poly enclosure of contact
    cpt.forceBinaryVar("b" + cntPos + "_1P");
    cpt.forceBinaryVar("b" + cntPos + "_2P");
    cpt.forceBinaryVar("b" + cntPos + "_3P");
    
    cpt.insertConstraint("ZERO", "x" + cntPos + "hP", CP_MIN, "b" + cntPos + "_1P", currentRules->getRule(E2P1CT));
    cpt.insertConstraint("ZERO", "x" + cntPos + "hP", CP_MIN, "b" + cntPos + "_2P", currentRules->getRule(E3P1CT));
    cpt.insertConstraint("ZERO", "x" + cntPos + "hP", CP_MIN, "b" + cntPos + "_3P", currentRules->getRule(E1P1CT));
    cpt.insertConstraint("ZERO", "b" + cntPos + "_1P" + " + " + "b" + cntPos + "_2P"  + " + " + "b" + cntPos + "_3P", CP_EQ, 1);
    cpt.insertConstraint("x" + polPos + "a", "x" + cntPos + "a", CP_MIN, "x" + cntPos + "hP");
    cpt.insertConstraint("x" + cntPos + "b", "x" + polPos + "b", CP_MIN, "x" + cntPos + "hP");
    cpt.insertConstraint("x" + polPos + "a", "x" + polPos + "b", CP_EQ, "x" + cntPos + "hminP");
    cpt.insertLPMinVar("x" + cntPos + "hminP",2);
    
    cpt.insertConstraint("ZERO", "y" + cntPos + "vP", CP_MIN, "b" + cntPos + "_1P", currentRules->getRule(E1P1CT));
    cpt.insertConstraint("ZERO", "y" + cntPos + "vP", CP_MIN, "b" + cntPos + "_2P", currentRules->getRule(E3P1CT));
    cpt.insertConstraint("ZERO", "y" + cntPos + "vP", CP_MIN, "b" + cntPos + "_3P", currentRules->getRule(E2P1CT));
    cpt.insertConstraint("y" + polPos + "a", "y" + cntPos + "a", CP_MIN, "y" + cntPos + "vP");
    cpt.insertConstraint("y" + cntPos + "b", "y" + polPos + "b", CP_MIN, "y" + cntPos + "vP");
    cpt.insertConstraint("y" + polPos + "a", "y" + polPos + "b", CP_EQ, "y" + cntPos + "vminP");
    cpt.insertLPMinVar("y" + cntPos + "vminP",2);

    return polPos;
}

void AutoCell::showIOCost() {
    for (list<Element>::iterator elements_it = elements.begin(); elements_it != elements.end(); elements_it++) {
        elements_it->print();
    }
}
