
// Query Optimizer — Web UI Application


const API_BASE = '';

//  Example Queries 
const EXAMPLES = {
    basic: `SELECT instructor.name, instructor.salary\nFROM instructor\nWHERE instructor.salary > 75000`,
    join2: `SELECT student.name, takes.course_id\nFROM student\nJOIN takes ON student.ID = takes.ID\nWHERE student.dept_name = 'Comp. Sci.'`,
    join3: `SELECT student.name, course.title\nFROM student\nJOIN takes ON student.ID = takes.ID\nJOIN course ON takes.course_id = course.course_id\nWHERE course.dept_name = 'Physics'`,
    complex: `SELECT instructor.name, instructor.salary\nFROM instructor\nWHERE instructor.dept_name = 'Finance' AND instructor.salary > 80000`,
    subquery: `SELECT sub.name\nFROM (SELECT instructor.name, instructor.salary FROM instructor WHERE instructor.salary > 60000) AS sub\nWHERE sub.salary < 90000`
};

function loadExample(key) {
    if (EXAMPLES[key]) {
        document.getElementById('sql-input').value = EXAMPLES[key];
    }
    document.getElementById('example-select').value = '';
}

//  Tab switching 
function switchTab(tabName) {
    document.querySelectorAll('.tab').forEach(t => t.classList.remove('active'));
    document.querySelectorAll('.tab-pane').forEach(p => p.classList.remove('active'));
    document.querySelector(`[data-tab="${tabName}"]`).classList.add('active');
    document.getElementById(`tab-${tabName}`).classList.add('active');
}

//  Catalog toggle 
function toggleCatalog() {
    const sidebar = document.getElementById('catalog-sidebar');
    if (sidebar.classList.contains('hidden')) {
        sidebar.classList.remove('hidden');
        fetchCatalog();
    } else {
        sidebar.classList.add('hidden');
    }
}

async function fetchCatalog() {
    try {
        const res = await fetch(`${API_BASE}/api/catalog`);
        const data = await res.json();
        renderCatalog(data);
    } catch (err) {
        document.getElementById('catalog-content').innerHTML =
            `<p style="color: var(--accent-red)">Failed to load catalog: ${err.message}</p>`;
    }
}

function renderCatalog(data) {
    const container = document.getElementById('catalog-content');
    if (!data.tables) { container.innerHTML = '<p>No tables.</p>'; return; }

    let html = '';
    for (const table of data.tables) {
        html += `<div class="catalog-table">
            <h3>📋 ${table.name}</h3>
            <div class="stat">${table.numTuples.toLocaleString()} rows · ${table.numBlocks} blocks · ${table.tupleSize}B/tuple</div>
            <table>
                <tr><th>Column</th><th>Type</th><th>V(A,r)</th><th>Key</th></tr>`;
        for (const col of table.columns) {
            let keyBadge = '';
            if (col.isPrimaryKey) keyBadge = '<span style="color:var(--accent-cyan)">PK</span>';
            else if (col.isForeignKey) keyBadge = `<span style="color:var(--accent-orange)">FK→${col.fkRefTable}</span>`;
            html += `<tr><td>${col.name}</td><td>${col.dataType}</td><td>${col.numDistinct}</td><td>${keyBadge}</td></tr>`;
        }
        html += '</table>';

        if (table.indices && table.indices.length > 0) {
            html += '<div style="margin-top:10px"><strong style="font-size:0.75rem;color:var(--text-muted)">INDICES:</strong><br>';
            for (const idx of table.indices) {
                const cls = idx.type === 'B+Tree' ? 'btree' : (idx.type === 'Hash' ? 'hash' : 'bitmap');
                const icon = idx.type === 'B+Tree' ? '🌲' : (idx.type === 'Hash' ? '#️⃣' : '🔲');
                html += `<span class="index-badge ${cls}">${icon} ${idx.type}(${idx.columns.join(', ')})`;
                if (idx.isClustered) html += ' <span class="index-clustered">clustered</span>';
                if (idx.isUnique) html += ' <span class="index-clustered">unique</span>';
                if (idx.type === 'B+Tree') html += ` h=${idx.height}`;
                html += `</span> `;
            }
            html += '</div>';
        }
        html += '</div>';
    }
    container.innerHTML = html;
}

//  Optimize Query 
async function optimizeQuery() {
    const sql = document.getElementById('sql-input').value.trim();
    if (!sql) return;

    const btn = document.getElementById('btn-optimize');
    btn.disabled = true;
    btn.innerHTML = '<span class="spinner"></span> Optimizing...';

    document.getElementById('error-panel').classList.add('hidden');
    document.getElementById('results-container').classList.add('hidden');

    try {
        const res = await fetch(`${API_BASE}/api/optimize`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ sql })
        });

        if (!res.ok) {
            const err = await res.json();
            throw new Error(err.error || 'Server error');
        }

        const data = await res.json();
        renderResults(data);
        document.getElementById('results-container').classList.remove('hidden');
    } catch (err) {
        document.getElementById('error-message').textContent = `Error: ${err.message}`;
        document.getElementById('error-panel').classList.remove('hidden');
    } finally {
        btn.disabled = false;
        btn.innerHTML = '<svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><polygon points="5 3 19 12 5 21 5 3"/></svg> Optimize';
    }
}

//  Render Results 
function renderResults(data) {
    // Cost summary
    const unopt = data.unoptimizedCost || 0;
    const opt = data.optimizedCost || 0;
    document.getElementById('cost-unopt').textContent = unopt.toFixed(1);
    document.getElementById('cost-opt').textContent = opt.toFixed(1);
    const improvement = unopt > 0 ? ((1 - opt / unopt) * 100) : 0;
    document.getElementById('cost-improve').textContent = improvement.toFixed(1);

    // Plan trees
    document.getElementById('tree-logical').innerHTML = renderPlanTree(data.logicalPlan);
    document.getElementById('tree-heuristic').innerHTML = renderPlanTree(data.heuristicPlan);
    document.getElementById('tree-unopt-physical').innerHTML = renderPhysicalTree(data.unoptimizedPhysicalPlan);
    document.getElementById('tree-physical').innerHTML = renderPhysicalTree(data.physicalPlan);

    // Cost comparison chart
    renderCostChart(data.allPlans || []);

    // Show logical tab by default
    switchTab('logical');
}

//  Render Logical Plan Tree 
function renderPlanTree(node, depth = 0) {
    if (!node || node === 'null') return '<p style="color:var(--text-muted)">No plan available</p>';
    if (typeof node === 'string') {
        try { node = JSON.parse(node); } catch { return '<p style="color:var(--text-muted)">Invalid plan data</p>'; }
    }

    const type = node.type || 'Unknown';
    const cssClass = `node-${type.toLowerCase()}`;
    const icons = { Scan: '📋', Select: 'σ', Project: 'π', Join: '⨝' };
    const icon = icons[type] || '?';

    let details = '';
    if (type === 'Scan') {
        details = node.table || '';
        if (node.alias) details += ` AS ${node.alias}`;
    } else if (type === 'Select') {
        details = predicateToString(node.predicate);
    } else if (type === 'Project') {
        details = (node.columns || []).map(c => `${c.table}.${c.column}`).join(', ');
    } else if (type === 'Join') {
        details = predicateToString(node.condition);
    }

    let rows = node.estimatedRows ? ` <span class="node-cost">${node.estimatedRows.toLocaleString()} rows</span>` : '';

    let html = `<div class="tree-node" style="animation-delay: ${depth * 0.08}s">
        <div class="node-card ${cssClass}">
            <div class="node-icon">${icon}</div>
            <span>${type}</span>
            <span class="node-details">${escapeHtml(details)}</span>
            ${rows}
        </div>`;

    // Render children
    if (node.input) html += renderPlanTree(node.input, depth + 1);
    if (node.left) html += renderPlanTree(node.left, depth + 1);
    if (node.right) html += renderPlanTree(node.right, depth + 1);

    html += '</div>';
    return html;
}

//  Render Physical Plan Tree 
function renderPhysicalTree(node, depth = 0) {
    if (!node || node === 'null') return '<p style="color:var(--text-muted)">No physical plan</p>';
    if (typeof node === 'string') {
        try { node = JSON.parse(node); } catch { return '<p style="color:var(--text-muted)">Invalid plan data</p>'; }
    }

    const type = node.type || 'Unknown';
    const cssMap = {
        'SeqScan': 'node-seq', 'B+TreeIndexScan': 'node-btree',
        'HashIndexScan': 'node-hash', 'BitmapIndexScan': 'node-bitmap',
        'Filter': 'node-filter', 'Projection': 'node-projection',
        'NestedLoopJoin': 'node-nl', 'BlockNestedLoopJoin': 'node-bnl',
        'MergeSortJoin': 'node-merge', 'HashJoin': 'node-hashjoin'
    };
    const cssClass = cssMap[type] || 'node-scan';
    const iconMap = {
        'SeqScan': '📋', 'B+TreeIndexScan': '🌲', 'HashIndexScan': '#️⃣',
        'BitmapIndexScan': '🔲', 'Filter': 'σ', 'Projection': 'π',
        'NestedLoopJoin': '⨝', 'BlockNestedLoopJoin': '⨝',
        'MergeSortJoin': '⨝', 'HashJoin': '⨝'
    };
    const icon = iconMap[type] || '?';

    let details = '';
    if (node.table) details += node.table;
    if (node.index) details += ` [${node.index}]`;
    if (node.predicate) details += ` | ${predicateToString(node.predicate)}`;
    if (node.joinCondition) details += ` ON ${predicateToString(node.joinCondition)}`;
    if (node.columns) details += (node.columns || []).map(c => `${c.table}.${c.column}`).join(', ');

    let badges = '';
    if (node.index) badges += `<span class="node-badge">IDX</span>`;
    const costStr = node.totalCost != null ? node.totalCost.toFixed(1) : '?';
    const rowStr = node.estimatedRows != null ? node.estimatedRows.toLocaleString() : '?';

    let html = `<div class="tree-node" style="animation-delay: ${depth * 0.08}s">
        <div class="node-card ${cssClass}">
            <div class="node-icon">${icon}</div>
            <span>${type}</span>
            <span class="node-details">${escapeHtml(details)}</span>
            <span class="node-cost">${costStr} ms · ${rowStr} rows</span>
            ${badges}
        </div>`;

    if (node.children) {
        for (const child of node.children) {
            html += renderPhysicalTree(child, depth + 1);
        }
    }

    html += '</div>';
    return html;
}

//  Cost Comparison Chart 
function renderCostChart(plans) {
    const container = document.getElementById('cost-chart');
    if (!plans || plans.length === 0) {
        container.innerHTML = '<p style="color:var(--text-muted)">No plan comparisons available</p>';
        return;
    }

    const maxCost = Math.max(...plans.map(p => p.cost));
    const bestCost = Math.min(...plans.map(p => p.cost));

    let html = '<h3 style="font-size:0.9rem;margin-bottom:16px;color:var(--text-primary)">All Enumerated Plans</h3>';

    plans.forEach((plan, i) => {
        const pct = maxCost > 0 ? (plan.cost / maxCost * 100) : 50;
        const barClass = plan.cost === bestCost ? 'best' : (plan.cost > maxCost * 0.7 ? 'worst' : 'other');

        html += `<div class="chart-bar-container" style="animation-delay: ${i * 0.06}s">
            <span class="chart-label">${escapeHtml(plan.description)}</span>
            <div class="chart-bar-wrapper">
                <div class="chart-bar ${barClass}" style="width: ${Math.max(pct, 8)}%">
                    ${plan.cost.toFixed(1)} ms
                </div>
            </div>
        </div>`;
    });

    container.innerHTML = html;
}

//  Utilities 
function predicateToString(pred) {
    if (!pred) return '';
    if (pred.type === 'comparison') {
        const left = pred.left.type === 'column' ? `${pred.left.table}.${pred.left.column}` : (pred.left.value ?? '');
        const right = pred.right.type === 'column' ? `${pred.right.table}.${pred.right.column}` : (pred.right.value ?? '');
        return `${left} ${pred.op} ${right}`;
    }
    if (pred.type === 'AND') return `${predicateToString(pred.left)} AND ${predicateToString(pred.right)}`;
    if (pred.type === 'OR') return `${predicateToString(pred.left)} OR ${predicateToString(pred.right)}`;
    if (pred.type === 'NOT') return `NOT (${predicateToString(pred.child)})`;
    return JSON.stringify(pred);
}

function escapeHtml(str) {
    if (!str) return '';
    return str.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;')
              .replace(/"/g, '&quot;').replace(/'/g, '&#039;');
}

//  Keyboard shortcut: Ctrl+Enter to optimize 
document.addEventListener('keydown', (e) => {
    if ((e.ctrlKey || e.metaKey) && e.key === 'Enter') {
        optimizeQuery();
    }
});
