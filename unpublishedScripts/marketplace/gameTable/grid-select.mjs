//
//  grid-select.mjs
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
//  Turning clicks on an 8x8 board into one of the published legal moves, for the client. Pure:
//  no engine globals. Each legal move comes with the cells it passes through, [from, ..., to].
//
//  Click one of your pieces (a cell some legal move starts on) to select it; its destinations
//  are offered. Click a destination to play the move there. When two different paths lead from
//  the selected piece to that destination (a checkers multi-jump), the next square on the way
//  is asked for until only one path is left. Moves that share a path (chess promotions) play
//  the queen's. Clicking another of your pieces reselects; any other click clears.
//

/**
 * @typedef {{ notation: string, cells: number[][] }} Choice
 */

/**
 * @param {number[]} a
 * @param {number[]} b
 * @returns {boolean}
 */
function same(a, b) {
    return a[0] === b[0] && a[1] === b[1];
}

/**
 * @param {number[][]} cells
 * @returns {number[][]} without repeats, in first-seen order
 */
function unique(cells) {
    return cells.filter((cell, index) => cells.findIndex(other => same(other, cell)) === index);
}

/**
 * @param {Choice[]} choices
 * @returns {string | null} the one move they amount to, or null when their paths differ
 */
function single(choices) {
    const path = (/** @type {Choice} */ choice) => choice.cells.map(cell => cell.join(",")).join(" ");
    if (choices.length === 0 || choices.some(choice => path(choice) !== path(choices[0]))) {
        return null;
    }
    const queen = choices.find(choice => choice.notation.endsWith("q"));
    return (queen || choices[0]).notation;
}

export class MovePicker {
    /** @type {Choice[]} */ choices = [];
    /** @type {number[][]} the selected piece's cell, then any squares on the way chosen so far */ prefix = [];
    /** @type {number[] | null} the destination being disambiguated */ destination = null;

    /** @param {Choice[]} choices the legal moves; [] when it is not this user's turn */
    setChoices(choices) {
        this.choices = choices;
        this.reset();
    }

    reset() {
        this.prefix = [];
        this.destination = null;
    }

    /**
     * @param {number[]} cell
     * @returns {boolean} whether some legal move starts on the cell
     */
    movable(cell) {
        return this.choices.some(choice => same(choice.cells[0], cell));
    }

    /**
     * @returns {Choice[]} the moves still possible from the selection
     */
    candidates() {
        const destination = this.destination;
        return this.choices.filter(choice => this.prefix.every((cell, i) => i < choice.cells.length && same(choice.cells[i], cell))
            && (destination === null || same(choice.cells[choice.cells.length - 1], destination)));
    }

    /**
     * @param {number[]} cell [row, col] of the clicked square
     * @returns {string | null} the move to send, if this click completes one
     */
    click(cell) {
        if (this.prefix.length === 0) {
            this.select(cell);
            return null;
        }
        if (this.destination === null) {
            const arriving = this.candidates().filter(choice => same(choice.cells[choice.cells.length - 1], cell));
            if (arriving.length) {
                const move = single(arriving);
                if (move) {
                    this.reset();
                    return move;
                }
                this.destination = cell;
                return null;
            }
            this.select(cell);
            return null;
        }
        const step = this.prefix.length;
        const onTheWay = this.candidates().filter(choice => step < choice.cells.length && same(choice.cells[step], cell));
        if (onTheWay.length === 0) {
            this.select(cell);
            return null;
        }
        this.prefix = [...this.prefix, cell];
        const move = single(onTheWay);
        if (move) {
            this.reset();
        }
        return move;
    }

    /**
     * @private
     * @param {number[]} cell
     */
    select(cell) {
        this.reset();
        if (this.movable(cell)) {
            this.prefix = [cell];
        }
    }

    /**
     * @returns {{ selected: number[][], targets: number[][] }} what to highlight: the selection
     *     (and the destination being disambiguated), and the squares a click can go to next
     */
    view() {
        if (this.prefix.length === 0) {
            return { selected: [], targets: [] };
        }
        const candidates = this.candidates();
        if (this.destination === null) {
            return { selected: this.prefix, targets: unique(candidates.map(choice => choice.cells[choice.cells.length - 1])) };
        }
        const step = this.prefix.length;
        return {
            selected: [...this.prefix, this.destination],
            targets: unique(candidates.filter(choice => step < choice.cells.length).map(choice => choice.cells[step]))
        };
    }
}
