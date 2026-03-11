class CirclesOfBattle {
    constructor() {
        this.boardSize = 12;
        this.board = [];
        this.phase = 'planning'; // 'planning', 'executing', 'resolution'
        this.turn = 1;
        this.gameOver = false;
        
        // Player orders - both plan simultaneously
        this.pendingOrders = {
            allied: [],
            axis: []
        };
        
        // Currently selected order being drawn
        this.currentOrder = null;
        this.selectedUnit = null;
        this.currentPlayer = 'allied'; // For viewing/hotseat play
        
        // Units
        this.units = [];
        this.nextUnitId = 1;
        
        // Objectives
        this.objectives = [];
        
        // Unit definitions based on Lines of Battle wiki
        this.unitTypes = {
            lineInfantry: {
                name: 'Line Infantry',
                hp: 800,
                org: 500,
                maxOrg: 500,
                meleeAtk: 28,
                meleeDef: 24,
                rangedAtk: { min: 13, max: 65 },
                move: 65,
                run: 115,
                chargeBonus: 20,
                chargePen: 25,
                orgRadius: 8,
                orgBonus: 56,
                symbol: 'LI',
                canShoot: true,
                canFallback: true,
                canFireAdvance: true,
                class: 'infantry'
            },
            guardInfantry: {
                name: 'Guard Infantry',
                hp: 800,
                org: 650,
                maxOrg: 650,
                meleeAtk: 30,
                meleeDef: 26,
                rangedAtk: { min: 21, max: 75 },
                move: 65,
                run: 115,
                chargeBonus: 28,
                chargePen: 35,
                orgRadius: 14,
                orgBonus: 72,
                symbol: 'GI',
                canShoot: true,
                canFallback: true,
                canFireAdvance: true,
                class: 'infantry'
            },
            hussars: {
                name: 'Hussars',
                hp: 500,
                org: 525,
                maxOrg: 525,
                meleeAtk: 32,
                meleeDef: 19,
                rangedAtk: null,
                move: 125,
                run: 195,
                chargeBonus: { min: 5, max: 70 },
                chargePen: 8,
                orgRadius: 64,
                orgBonus: 6,
                symbol: 'HU',
                canShoot: false,
                canFallback: true,
                canFireAdvance: false,
                class: 'cavalry'
            },
            cuirassiers: {
                name: 'Cuirassiers',
                hp: 550,
                org: 575,
                maxOrg: 575,
                meleeAtk: 37,
                meleeDef: 30,
                rangedAtk: null,
                move: 125,
                run: 190,
                chargeBonus: { min: 14, max: 80 },
                chargePen: 18,
                orgRadius: 96,
                orgBonus: 6,
                symbol: 'CU',
                canShoot: false,
                canFallback: true,
                canFireAdvance: false,
                class: 'cavalry'
            },
            footArtillery: {
                name: '12-lb Foot Artillery',
                hp: 800,
                org: 300,
                maxOrg: 300,
                meleeAtk: 6,
                meleeDef: 7,
                rangedAtk: { min: 43, max: 43 },
                range: { close: 145, medium: 250, long: 500 },
                move: 20,
                run: 105,
                limberMove: 105,
                chargeBonus: { min: 0, max: 5 },
                chargeRes: -75,
                orgRadius: 32,
                orgBonus: 2,
                symbol: 'FA',
                canShoot: true,
                canFallback: false,
                canFireAdvance: false,
                class: 'artillery'
            },
            horseArtillery: {
                name: '6-lb Horse Artillery',
                hp: 800,
                org: 300,
                maxOrg: 300,
                meleeAtk: 6,
                meleeDef: 7,
                rangedAtk: { min: 34, max: 34 },
                range: { close: 120, medium: 230, long: 400 },
                move: 60,
                run: 170,
                limberMove: 170,
                chargeBonus: { min: 0, max: 5 },
                chargeRes: -75,
                orgRadius: 32,
                orgBonus: 2,
                symbol: 'HA',
                canShoot: true,
                canFallback: false,
                canFireAdvance: false,
                class: 'artillery'
            }
        };
        
        this.init();
    }
    
    init() {
        this.createBoard();
        this.setupObjectives();
        this.setupPieces();
        this.render();
        this.setupEventListeners();
    }
    
    createBoard() {
        const battlefield = document.getElementById('battlefield');
        battlefield.innerHTML = '';
        battlefield.style.gridTemplateColumns = `repeat(${this.boardSize}, 1fr)`;
        
        for (let row = 0; row < this.boardSize; row++) {
            for (let col = 0; col < this.boardSize; col++) {
                const cell = document.createElement('div');
                cell.className = 'cell';
                cell.dataset.row = row;
                cell.dataset.col = col;
                battlefield.appendChild(cell);
            }
        }
    }
    
    setupObjectives() {
        // Central neutral objective
        this.objectives.push({
            id: 'obj1',
            row: 6,
            col: 6,
            type: 'neutral',
            size: 'small',
            captureRate: 8,
            controlledBy: null,
            captureProgress: { allied: 0, axis: 0 }
        });
        
        // Allied base (south)
        this.objectives.push({
            id: 'alliedBase',
            row: 10,
            col: 6,
            type: 'base',
            size: 'large',
            captureRate: 32,
            controlledBy: 'allied',
            captureProgress: { allied: 32, axis: 0 }
        });
        
        // Axis base (north)
        this.objectives.push({
            id: 'axisBase',
            row: 2,
            col: 6,
            type: 'base',
            size: 'large',
            captureRate: 32,
            controlledBy: 'axis',
            captureProgress: { allied: 0, axis: 32 }
        });
    }
    
    setupPieces() {
        this.units = [];
        
        // Allied setup (south, facing north)
        const alliedUnits = [
            { type: 'lineInfantry', row: 9, col: 3 },
            { type: 'lineInfantry', row: 9, col: 4 },
            { type: 'lineInfantry', row: 9, col: 5 },
            { type: 'lineInfantry', row: 9, col: 6 },
            { type: 'guardInfantry', row: 10, col: 3 },
            { type: 'guardInfantry', row: 10, col: 4 },
            { type: 'hussars', row: 9, col: 1 },
            { type: 'hussars', row: 9, col: 8 },
            { type: 'footArtillery', row: 10, col: 1 },
            { type: 'horseArtillery', row: 10, col: 8 }
        ];
        
        // Axis setup (north, facing south)
        const axisUnits = [
            { type: 'lineInfantry', row: 3, col: 3 },
            { type: 'lineInfantry', row: 3, col: 4 },
            { type: 'lineInfantry', row: 3, col: 5 },
            { type: 'lineInfantry', row: 3, col: 6 },
            { type: 'guardInfantry', row: 2, col: 3 },
            { type: 'guardInfantry', row: 2, col: 4 },
            { type: 'hussars', row: 3, col: 1 },
            { type: 'hussars', row: 3, col: 8 },
            { type: 'footArtillery', row: 2, col: 1 },
            { type: 'horseArtillery', row: 2, col: 8 }
        ];
        
        alliedUnits.forEach(u => this.createUnit(u.type, u.row, u.col, 'allied', 'N'));
        axisUnits.forEach(u => this.createUnit(u.type, u.row, u.col, 'axis', 'S'));
    }
    
    createUnit(type, row, col, player, facing) {
        const unitType = this.unitTypes[type];
        const unit = {
            id: this.nextUnitId++,
            type: type,
            ...JSON.parse(JSON.stringify(unitType)), // Deep copy
            player: player,
            row: row,
            col: col,
            facing: facing,
            hasOrder: false,
            currentOrder: null,
            path: [],
            limbered: false,
            inCombat: false
        };
        this.units.push(unit);
        return unit;
    }
    
    getUnitAt(row, col) {
        return this.units.find(u => u.row === row && u.col === col && !u.destroyed);
    }
    
    getFacingArrow(facing) {
        const arrows = { N: '↑', S: '↓', E: '→', W: '←' };
        return arrows[facing] || '↑';
    }
    
    getFacingRotation(facing) {
        const rotations = { N: 0, E: 90, S: 180, W: 270 };
        return rotations[facing] || 0;
    }
    
    render() {
        const cells = document.querySelectorAll('.cell');
        cells.forEach(cell => {
            const row = parseInt(cell.dataset.row);
            const col = parseInt(cell.dataset.col);
            
            cell.innerHTML = '';
            cell.className = 'cell';
            
            // Check for terrain
            this.renderTerrain(cell, row, col);
            
            // Check for objectives
            const obj = this.objectives.find(o => o.row === row && o.col === col);
            if (obj) {
                this.renderObjective(cell, obj);
            }
            
            // Check for units
            const unit = this.getUnitAt(row, col);
            if (unit) {
                this.renderUnit(cell, unit);
            }
            
            // Show planned path if this is part of current order
            if (this.currentOrder && this.currentOrder.path) {
                const pathIndex = this.currentOrder.path.findIndex(p => p.row === row && p.col === col);
                if (pathIndex !== -1) {
                    cell.classList.add('path-marker');
                    const marker = document.createElement('div');
                    marker.className = 'waypoint';
                    marker.textContent = pathIndex + 1;
                    cell.appendChild(marker);
                }
            }
            
            // Show selected unit
            if (this.selectedUnit && this.selectedUnit.id === unit?.id) {
                cell.classList.add('selected');
            }
        });
        
        this.updateUI();
    }
    
    renderTerrain(cell, row, col) {
        // Add some terrain features
        if ((row >= 5 && row <= 7) && (col >= 4 && col <= 8)) {
            cell.classList.add('terrain-woods');
        }
        if ((row === 6 || row === 7) && (col === 2 || col === 3)) {
            cell.classList.add('terrain-hill');
        }
    }
    
    renderObjective(cell, obj) {
        const objEl = document.createElement('div');
        objEl.className = `objective ${obj.type} ${obj.controlledBy || 'neutral'}`;
        objEl.style.width = obj.size === 'large' ? '60%' : '40%';
        objEl.style.height = obj.size === 'large' ? '60%' : '40%';
        
        const icon = document.createElement('span');
        icon.className = 'objective-icon';
        icon.textContent = obj.type === 'base' ? '★' : '⚑';
        objEl.appendChild(icon);
        
        // Show capture progress
        if (obj.captureProgress.allied > 0 || obj.captureProgress.axis > 0) {
            const progress = document.createElement('div');
            progress.className = 'capture-progress';
            const totalProgress = obj.captureProgress.allied + obj.captureProgress.axis;
            const alliedPercent = (obj.captureProgress.allied / totalProgress) * 100;
            progress.innerHTML = `
                <div class="progress-bar allied" style="width: ${alliedPercent}%"></div>
                <div class="progress-bar axis" style="width: ${100 - alliedPercent}%"></div>
            `;
            objEl.appendChild(progress);
        }
        
        cell.appendChild(objEl);
    }
    
    renderUnit(cell, unit) {
        const unitEl = document.createElement('div');
        unitEl.className = `unit ${unit.player} ${unit.type} ${unit.hasOrder ? 'has-order' : ''} ${unit.inCombat ? 'in-combat' : ''}`;
        unitEl.style.transform = `rotate(${this.getFacingRotation(unit.facing)}deg)`;
        
        // Formation shape (not just a circle)
        const formation = document.createElement('div');
        formation.className = 'unit-formation';
        formation.innerHTML = this.getFormationShape(unit);
        unitEl.appendChild(formation);
        
        // Unit info overlay
        const info = document.createElement('div');
        info.className = 'unit-info';
        info.innerHTML = `
            <span class="unit-symbol">${unit.symbol}</span>
            <span class="unit-hp">${Math.floor(unit.hp / 8)}%</span>
        `;
        unitEl.appendChild(info);
        
        // Organization bar
        const orgBar = document.createElement('div');
        orgBar.className = 'org-bar';
        const orgPercent = (unit.org / unit.maxOrg) * 100;
        orgBar.innerHTML = `<div class="org-fill" style="width: ${orgPercent}%; background: ${orgPercent < 30 ? '#ff6b6b' : orgPercent < 60 ? '#ffd93d' : '#4a8c4a'}"></div>`;
        unitEl.appendChild(orgBar);
        
        // Show order icon if has order
        if (unit.hasOrder && unit.currentOrder) {
            const orderIcon = document.createElement('div');
            orderIcon.className = 'order-icon';
            orderIcon.textContent = this.getOrderIcon(unit.currentOrder.type);
            unitEl.appendChild(orderIcon);
        }
        
        cell.appendChild(unitEl);
    }
    
    getFormationShape(unit) {
        // Different shapes for different unit types
        const shapes = {
            lineInfantry: '<div class="formation-line"></div><div class="formation-line"></div>',
            guardInfantry: '<div class="formation-line heavy"></div><div class="formation-line heavy"></div>',
            hussars: '<div class="formation-column cavalry"></div>',
            cuirassiers: '<div class="formation-column cavalry heavy"></div>',
            footArtillery: '<div class="formation-battery"></div>',
            horseArtillery: '<div class="formation-battery mobile"></div>'
        };
        return shapes[unit.type] || '<div class="formation-line"></div>';
    }
    
    getOrderIcon(orderType) {
        const icons = {
            move: '→',
            seek: '◎',
            shoot: '⌖',
            fireAdvance: '↗',
            fallback: '↘'
        };
        return icons[orderType] || '?';
    }
    
    setupEventListeners() {
        const battlefield = document.getElementById('battlefield');
        
        battlefield.addEventListener('click', (e) => {
            if (this.phase !== 'planning') return;
            
            const cell = e.target.closest('.cell');
            if (!cell) return;
            
            const row = parseInt(cell.dataset.row);
            const col = parseInt(cell.dataset.col);
            this.handleCellClick(row, col);
        });
        
        // Right click to cancel
        battlefield.addEventListener('contextmenu', (e) => {
            e.preventDefault();
            if (this.currentOrder) {
                this.cancelOrder();
            } else if (this.selectedUnit) {
                this.selectedUnit = null;
                this.render();
            }
        });
    }
    
    handleCellClick(row, col) {
        if (this.currentOrder) {
            // Continue drawing path
            this.addPathPoint(row, col);
            return;
        }
        
        const unit = this.getUnitAt(row, col);
        
        // Select unit
        if (unit && unit.player === this.currentPlayer && !unit.hasOrder) {
            this.selectedUnit = unit;
            this.render();
        }
    }
    
    startOrder(orderType) {
        if (!this.selectedUnit) return;
        if (this.phase !== 'planning') return;
        if (this.selectedUnit.hasOrder) return;
        
        // Validate order type for unit
        const unit = this.selectedUnit;
        
        if (orderType === 'shoot' && !unit.canShoot) {
            alert(`${unit.name} cannot shoot!`);
            return;
        }
        if (orderType === 'fireAdvance' && !unit.canFireAdvance) {
            alert(`${unit.name} cannot use Fire & Advance!`);
            return;
        }
        if (orderType === 'fallback' && !unit.canFallback) {
            alert(`${unit.name} cannot fallback!`);
            return;
        }
        
        this.currentOrder = {
            unitId: unit.id,
            type: orderType,
            path: [{ row: unit.row, col: unit.col }],
            target: null
        };
        
        // For seek, shoot, need to select target
        if (orderType === 'seek' || orderType === 'shoot') {
            // Highlight valid targets
            this.highlightTargets(orderType);
        }
    }
    
    addPathPoint(row, col) {
        if (!this.currentOrder) return;
        
        const lastPoint = this.currentOrder.path[this.currentOrder.path.length - 1];
        
        // Check if adjacent
        const dRow = Math.abs(row - lastPoint.row);
        const dCol = Math.abs(col - lastPoint.col);
        
        if (dRow <= 1 && dCol <= 1 && !(dRow === 0 && dCol === 0)) {
            // Check range limit
            const unit = this.units.find(u => u.id === this.currentOrder.unitId);
            const pathLength = this.currentOrder.path.length;
            
            if (this.currentOrder.type === 'move' || this.currentOrder.type === 'fireAdvance') {
                // Limited by unit movement
                const maxMove = unit.limbered ? unit.limberMove : unit.move;
                // Simplified: count steps
                if (pathLength < maxMove / 20) { // Rough estimate
                    this.currentOrder.path.push({ row, col });
                    this.render();
                }
            } else if (this.currentOrder.type === 'fallback') {
                // 25% slower
                if (pathLength < (unit.move * 0.75) / 20) {
                    this.currentOrder.path.push({ row, col });
                    this.render();
                }
            }
        }
    }
    
    highlightTargets(orderType) {
        // This would highlight valid targets for seek/shoot
        // Simplified implementation
        this.render();
    }
    
    setTarget(row, col) {
        if (!this.currentOrder) return;
        
        const targetUnit = this.getUnitAt(row, col);
        if (targetUnit && targetUnit.player !== this.currentPlayer) {
            this.currentOrder.target = { row, col, unitId: targetUnit.id };
            this.confirmOrder();
        }
    }
    
    confirmOrder() {
        if (!this.currentOrder) return;
        
        // Add to pending orders
        this.pendingOrders[this.currentPlayer].push(this.currentOrder);
        
        // Mark unit as having order
        const unit = this.units.find(u => u.id === this.currentOrder.unitId);
        unit.hasOrder = true;
        unit.currentOrder = this.currentOrder;
        
        this.currentOrder = null;
        this.selectedUnit = null;
        this.render();
    }
    
    cancelOrder() {
        this.currentOrder = null;
        this.selectedUnit = null;
        this.render();
    }
    
    removeOrder(unitId) {
        const player = this.currentPlayer;
        const idx = this.pendingOrders[player].findIndex(o => o.unitId === unitId);
        if (idx !== -1) {
            this.pendingOrders[player].splice(idx, 1);
            const unit = this.units.find(u => u.id === unitId);
            unit.hasOrder = false;
            unit.currentOrder = null;
            this.render();
        }
    }
    
    executeTurn() {
        if (this.phase !== 'planning') return;
        
        this.phase = 'executing';
        this.updateUI();
        
        // Execute orders for both sides
        this.resolveOrders();
        
        // After execution, go back to planning
        setTimeout(() => {
            this.endTurn();
        }, 2000);
    }
    
    resolveOrders() {
        // Combine and sort orders by unit speed (faster units act first)
        const allOrders = [
            ...this.pendingOrders.allied.map(o => ({ ...o, player: 'allied' })),
            ...this.pendingOrders.axis.map(o => ({ ...o, player: 'axis' }))
        ];
        
        // Sort by unit speed
        allOrders.sort((a, b) => {
            const unitA = this.units.find(u => u.id === a.unitId);
            const unitB = this.units.find(u => u.id === b.unitId);
            return unitB.run - unitA.run;
        });
        
        // Execute each order
        allOrders.forEach(order => {
            this.executeOrder(order);
        });
        
        // Check for melee combat between adjacent units
        this.resolveMelee();
        
        // Update objectives
        this.updateObjectives();
        
        this.render();
    }
    
    executeOrder(order) {
        const unit = this.units.find(u => u.id === order.unitId);
        if (!unit || unit.hp <= 0) return;
        
        switch (order.type) {
            case 'move':
                this.executeMove(unit, order);
                break;
            case 'seek':
                this.executeSeek(unit, order);
                break;
            case 'shoot':
                this.executeShoot(unit, order);
                break;
            case 'fireAdvance':
                this.executeFireAdvance(unit, order);
                break;
            case 'fallback':
                this.executeFallback(unit, order);
                break;
        }
    }
    
    executeMove(unit, order) {
        if (!order.path || order.path.length < 2) return;
        
        const destination = order.path[order.path.length - 1];
        const targetUnit = this.getUnitAt(destination.row, destination.col);
        
        if (targetUnit && targetUnit.player !== unit.player) {
            // Charge into melee
            this.resolveCharge(unit, targetUnit, order.path.length);
        } else if (!targetUnit) {
            // Normal move
            unit.row = destination.row;
            unit.col = destination.col;
            
            // Update facing based on last movement direction
            const prev = order.path[order.path.length - 2];
            if (destination.row > prev.row) unit.facing = 'S';
            else if (destination.row < prev.row) unit.facing = 'N';
            else if (destination.col > prev.col) unit.facing = 'E';
            else if (destination.col < prev.col) unit.facing = 'W';
        }
    }
    
    executeSeek(unit, order) {
        if (!order.target) return;
        
        const target = this.units.find(u => u.id === order.target.unitId);
        if (!target || target.hp <= 0) return;
        
        // Calculate path to target
        const path = this.calculatePath(unit.row, unit.col, target.row, target.col);
        
        if (path.length > 1) {
            const destination = path[path.length - 1];
            const targetUnit = this.getUnitAt(destination.row, destination.col);
            
            if (targetUnit && targetUnit.player !== unit.player) {
                this.resolveCharge(unit, targetUnit, path.length);
            } else {
                unit.row = destination.row;
                unit.col = destination.col;
            }
        }
    }
    
    executeShoot(unit, order) {
        if (!order.target) return;
        
        const target = this.units.find(u => u.id === order.target.unitId);
        if (!target || target.hp <= 0) return;
        
        // Check range
        const distance = Math.sqrt(
            Math.pow(target.row - unit.row, 2) + 
            Math.pow(target.col - unit.col, 2)
        );
        
        const maxRange = unit.range ? unit.range.long : 2;
        if (distance > maxRange / 50) return; // Rough conversion
        
        // Calculate damage
        const baseDamage = typeof unit.rangedAtk === 'object' ? 
            (unit.rangedAtk.min + unit.rangedAtk.max) / 2 : unit.rangedAtk;
        
        const damage = Math.floor(baseDamage * (0.8 + Math.random() * 0.4));
        
        // Apply damage to HP and Org
        target.hp = Math.max(0, target.hp - damage);
        target.org = Math.max(0, target.org - damage * 0.7);
        
        if (target.hp <= 0) {
            target.destroyed = true;
        }
    }
    
    executeFireAdvance(unit, order) {
        // Move at 75% speed
        if (order.path && order.path.length > 1) {
            const maxSteps = Math.floor(order.path.length * 0.75);
            const destination = order.path[Math.min(maxSteps, order.path.length - 1)];
            
            if (!this.getUnitAt(destination.row, destination.col)) {
                unit.row = destination.row;
                unit.col = destination.col;
            }
        }
        
        // Find targets in range and fire
        this.autoFire(unit, 0.75); // -25% damage
    }
    
    executeFallback(unit, order) {
        // Move backwards (face away)
        if (order.path && order.path.length > 1) {
            const maxSteps = Math.floor(order.path.length * 0.7); // -30% speed
            const destination = order.path[Math.min(maxSteps, order.path.length - 1)];
            
            if (!this.getUnitAt(destination.row, destination.col)) {
                unit.row = destination.row;
                unit.col = destination.col;
                
                // Reverse facing
                const facingMap = { N: 'S', S: 'N', E: 'W', W: 'E' };
                unit.facing = facingMap[unit.facing];
            }
        }
        
        // Can fire while retreating
        if (unit.canShoot) {
            this.autoFire(unit, 0.65); // -35% damage
        }
    }
    
    resolveCharge(attacker, defender, chargeDistance) {
        // Move into contact
        attacker.row = defender.row;
        attacker.col = defender.col;
        attacker.inCombat = true;
        defender.inCombat = true;
        
        // Calculate charge bonus based on distance
        const chargeBonus = typeof attacker.chargeBonus === 'object' ?
            attacker.chargeBonus.min + (attacker.chargeBonus.max - attacker.chargeBonus.min) * Math.min(chargeDistance / 5, 1) :
            attacker.chargeBonus;
        
        // Melee combat
        const attackerPower = attacker.meleeAtk * (1 + chargeBonus / 100);
        const defenderPower = defender.meleeDef;
        
        const attackerRoll = Math.random() * attackerPower;
        const defenderRoll = Math.random() * defenderPower;
        
        const damage = Math.floor((attackerRoll + defenderRoll) / 2);
        
        // Both sides take damage
        attacker.hp = Math.max(0, attacker.hp - Math.floor(defenderRoll));
        attacker.org = Math.max(0, attacker.org - Math.floor(defenderRoll * 0.8));
        
        defender.hp = Math.max(0, defender.hp - Math.floor(attackerRoll));
        defender.org = Math.max(0, defender.org - Math.floor(attackerRoll * 0.8));
        
        if (attacker.hp <= 0) attacker.destroyed = true;
        if (defender.hp <= 0) defender.destroyed = true;
    }
    
    autoFire(unit, damageModifier) {
        // Find enemy units in front arc
        const targets = this.units.filter(u => 
            u.player !== unit.player && 
            u.hp > 0 &&
            this.isInFrontArc(unit, u) &&
            this.getDistance(unit, u) <= (unit.range ? unit.range.long : 2) / 50
        );
        
        if (targets.length > 0) {
            // Fire at closest target
            targets.sort((a, b) => this.getDistance(unit, a) - this.getDistance(unit, b));
            const target = targets[0];
            
            const baseDamage = typeof unit.rangedAtk === 'object' ? 
                (unit.rangedAtk.min + unit.rangedAtk.max) / 2 : unit.rangedAtk;
            
            const damage = Math.floor(baseDamage * damageModifier * (0.8 + Math.random() * 0.4));
            
            target.hp = Math.max(0, target.hp - damage);
            target.org = Math.max(0, target.org - damage * 0.7);
            
            if (target.hp <= 0) target.destroyed = true;
        }
    }
    
    resolveMelee() {
        // Handle ongoing melee between adjacent units
        this.units.forEach(unit => {
            if (unit.hp <= 0) return;
            
            // Find adjacent enemies
            const adjacent = [
                [unit.row - 1, unit.col],
                [unit.row + 1, unit.col],
                [unit.row, unit.col - 1],
                [unit.row, unit.col + 1]
            ];
            
            adjacent.forEach(([row, col]) => {
                const enemy = this.getUnitAt(row, col);
                if (enemy && enemy.player !== unit.player && enemy.hp > 0) {
                    // Ongoing melee
                    const attackerPower = unit.meleeAtk;
                    const defenderPower = enemy.meleeDef;
                    
                    const attackerRoll = Math.random() * attackerPower;
                    const defenderRoll = Math.random() * defenderPower;
                    
                    unit.hp = Math.max(0, unit.hp - Math.floor(defenderRoll));
                    unit.org = Math.max(0, unit.org - Math.floor(defenderRoll * 0.5));
                    
                    enemy.hp = Math.max(0, enemy.hp - Math.floor(attackerRoll));
                    enemy.org = Math.max(0, enemy.org - Math.floor(attackerRoll * 0.5));
                    
                    if (unit.hp <= 0) unit.destroyed = true;
                    if (enemy.hp <= 0) enemy.destroyed = true;
                }
            });
        });
    }
    
    calculatePath(startRow, startCol, endRow, endCol) {
        // Simple pathfinding
        const path = [{ row: startRow, col: startCol }];
        let currentRow = startRow;
        let currentCol = startCol;
        
        while (currentRow !== endRow || currentCol !== endCol) {
            if (currentRow < endRow) currentRow++;
            else if (currentRow > endRow) currentRow--;
            
            if (currentCol < endCol) currentCol++;
            else if (currentCol > endCol) currentCol--;
            
            path.push({ row: currentRow, col: currentCol });
            
            if (path.length > 20) break; // Safety limit
        }
        
        return path;
    }
    
    getDistance(unitA, unitB) {
        return Math.sqrt(
            Math.pow(unitB.row - unitA.row, 2) + 
            Math.pow(unitB.col - unitB.col, 2)
        );
    }
    
    isInFrontArc(attacker, target) {
        const dRow = target.row - attacker.row;
        const dCol = target.col - attacker.col;
        
        switch (attacker.facing) {
            case 'N': return dRow < 0;
            case 'S': return dRow > 0;
            case 'E': return dCol > 0;
            case 'W': return dCol < 0;
            default: return false;
        }
    }
    
    updateObjectives() {
        this.objectives.forEach(obj => {
            // Count units in capture radius
            const radius = 4;
            const unitsInRadius = this.units.filter(u => 
                u.hp > 0 &&
                u.class !== 'artillery' && // Artillery can't capture
                Math.abs(u.row - obj.row) <= radius &&
                Math.abs(u.col - obj.col) <= radius
            );
            
            const alliedUnits = unitsInRadius.filter(u => u.player === 'allied').length;
            const axisUnits = unitsInRadius.filter(u => u.player === 'axis').length;
            
            // Infantry = 1 point, Cavalry = 0.25 points
            const alliedPoints = unitsInRadius
                .filter(u => u.player === 'allied')
                .reduce((sum, u) => sum + (u.class === 'infantry' ? 1 : 0.25), 0);
            
            const axisPoints = unitsInRadius
                .filter(u => u.player === 'axis')
                .reduce((sum, u) => sum + (u.class === 'infantry' ? 1 : 0.25), 0);
            
            // Block capture if enemies present
            if (alliedPoints > 0 && axisPoints === 0) {
                obj.captureProgress.allied = Math.min(obj.captureRate, obj.captureProgress.allied + alliedPoints);
                obj.captureProgress.axis = Math.max(0, obj.captureProgress.axis - alliedPoints);
            } else if (axisPoints > 0 && alliedPoints === 0) {
                obj.captureProgress.axis = Math.min(obj.captureRate, obj.captureProgress.axis + axisPoints);
                obj.captureProgress.allied = Math.max(0, obj.captureProgress.allied - axisPoints);
            }
            
            // Check capture
            if (obj.captureProgress.allied >= obj.captureRate) {
                obj.controlledBy = 'allied';
            } else if (obj.captureProgress.axis >= obj.captureRate) {
                obj.controlledBy = 'axis';
            }
            
            // Check win condition for bases
            if (obj.type === 'base') {
                if (obj.controlledBy === 'allied' && obj.id === 'axisBase') {
                    this.gameOver = true;
                    this.showModal('Allied Victory!');
                } else if (obj.controlledBy === 'axis' && obj.id === 'alliedBase') {
                    this.gameOver = true;
                    this.showModal('Axis Victory!');
                }
            }
        });
    }
    
    endTurn() {
        // Clear orders
        this.units.forEach(u => {
            u.hasOrder = false;
            u.currentOrder = null;
            u.inCombat = false;
        });
        
        this.pendingOrders.allied = [];
        this.pendingOrders.axis = [];
        
        // Switch planning to other player in hotseat mode
        this.currentPlayer = this.currentPlayer === 'allied' ? 'axis' : 'allied';
        
        this.phase = 'planning';
        this.turn++;
        
        this.render();
    }
    
    updateUI() {
        // Update phase display
        const status = document.getElementById('game-status');
        if (this.phase === 'planning') {
            status.textContent = `${this.currentPlayer === 'allied' ? 'Allied' : 'Axis'} Planning Phase - Turn ${this.turn}`;
        } else {
            status.textContent = `Executing Orders...`;
        }
        
        // Update player indicators
        document.getElementById('allied-indicator').classList.toggle('active', this.currentPlayer === 'allied');
        document.getElementById('axis-indicator').classList.toggle('active', this.currentPlayer === 'axis');
        
        // Update button states
        const orderButtons = document.querySelectorAll('.order-btn');
        orderButtons.forEach(btn => {
            btn.disabled = this.phase !== 'planning' || !this.selectedUnit;
        });
        
        document.getElementById('execute-btn').disabled = this.phase !== 'planning';
        document.getElementById('end-planning-btn').disabled = this.phase !== 'planning';
    }
    
    showModal(message) {
        const modal = document.createElement('div');
        modal.className = 'modal';
        modal.innerHTML = `
            <div class="modal-content">
                <h2>${message}</h2>
                <button class="btn" onclick="this.closest('.modal').remove(); game.resetGame()">Play Again</button>
            </div>
        `;
        document.body.appendChild(modal);
    }
    
    resetGame() {
        this.units = [];
        this.nextUnitId = 1;
        this.pendingOrders = { allied: [], axis: [] };
        this.currentOrder = null;
        this.selectedUnit = null;
        this.phase = 'planning';
        this.turn = 1;
        this.currentPlayer = 'allied';
        this.gameOver = false;
        
        this.setupPieces();
        this.render();
    }
}

// Start the game
const game = new CirclesOfBattle();