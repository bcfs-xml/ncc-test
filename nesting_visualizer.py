import json
import plotly.graph_objects as go
import plotly.express as px
from plotly.subplots import make_subplots
import numpy as np

def load_nesting_data(filename):
    """Load nesting results from JSON file."""
    with open(filename, 'r', encoding='utf-8') as f:
        return json.load(f)

def create_board_visualization(board_data, board_x, board_y):
    """Create a visualization for a single board with all its pieces."""
    fig = go.Figure()
    
    # Add board boundary as a rectangle
    fig.add_shape(
        type="rect",
        x0=0, y0=0, x1=board_x, y1=board_y,
        line=dict(color="black", width=2),
        fillcolor="rgba(240, 240, 240, 0.2)",
        name="Board"
    )
    
    # Color palette for different pieces
    colors = px.colors.qualitative.Set1
    
    for i, piece in enumerate(board_data['pieces']):
        piece_id = piece['piece_id']
        color = colors[i % len(colors)]
        
        # Extract x and y coordinates from the data array
        coordinates = piece['data']
        if coordinates:
            x_coords = [coord[0] for coord in coordinates]
            y_coords = [coord[1] for coord in coordinates]
            
            # Close the polygon by adding the first point at the end
            x_coords.append(x_coords[0])
            y_coords.append(y_coords[0])
            
            # Add piece as a filled polygon
            fig.add_trace(go.Scatter(
                x=x_coords,
                y=y_coords,
                fill='tonext',
                fillcolor=color,
                line=dict(color=color, width=2),
                mode='lines',
                name=f'Piece {piece_id}',
                hovertemplate=(
                    f'<b>Piece {piece_id}</b><br>' +
                    f'Position: ({piece["position_x"]}, {piece["position_y"]})<br>' +
                    f'Angle: {piece["angle"]}°<br>' +
                    '<extra></extra>'
                )
            ))
    
    # Update layout
    fig.update_layout(
        title=f'Board {board_data["board_id"]} - Efficiency: {board_data["efficiency"]:.1f}%',
        xaxis=dict(
            title='X Coordinate',
            showgrid=True,
            gridwidth=1,
            gridcolor='lightgray',
            range=[-50, board_x + 50]
        ),
        yaxis=dict(
            title='Y Coordinate',
            showgrid=True,
            gridwidth=1,
            gridcolor='lightgray',
            range=[-50, board_y + 50],
            scaleanchor="x",
            scaleratio=1
        ),
        showlegend=True,
        width=800,
        height=600,
        plot_bgcolor='white'
    )
    
    return fig

def create_efficiency_dashboard(data):
    """Create a dashboard showing efficiency metrics and board overview."""
    boards = data['boards']
    
    # Create subplots: 2 rows, 2 columns
    fig = make_subplots(
        rows=2, cols=2,
        subplot_titles=[
            'Board Efficiency Comparison',
            'Piece Count per Board',
            'Total Efficiency Overview',
            'Execution Summary'
        ],
        specs=[[{"type": "xy"}, {"type": "xy"}],
               [{"type": "indicator"}, {"type": "table"}]]
    )
    
    # Board efficiency bar chart
    board_ids = [f"Board {board['board_id']}" for board in boards]
    efficiencies = [board['efficiency'] for board in boards]
    piece_counts = [board['piece_count'] for board in boards]
    
    fig.add_trace(
        go.Bar(
            x=board_ids,
            y=efficiencies,
            text=[f"{eff:.1f}%" for eff in efficiencies],
            textposition='auto',
            marker_color=px.colors.sequential.Viridis,
            name='Efficiency'
        ),
        row=1, col=1
    )
    
    # Piece count bar chart
    fig.add_trace(
        go.Bar(
            x=board_ids,
            y=piece_counts,
            text=piece_counts,
            textposition='auto',
            marker_color=px.colors.sequential.Plasma,
            name='Piece Count'
        ),
        row=1, col=2
    )
    
    # Total efficiency indicator
    fig.add_trace(
        go.Indicator(
            mode="gauge+number+delta",
            value=data['total_efficiency'],
            domain={'x': [0, 1], 'y': [0, 1]},
            title={'text': "Total Efficiency (%)"},
            delta={'reference': 50, 'valueformat': '.1f'},
            gauge={
                'axis': {'range': [None, 100]},
                'bar': {'color': "darkgreen"},
                'steps': [
                    {'range': [0, 25], 'color': "lightgray"},
                    {'range': [25, 50], 'color': "yellow"},
                    {'range': [50, 75], 'color': "orange"},
                    {'range': [75, 100], 'color': "lightgreen"}
                ],
                'threshold': {
                    'line': {'color': "red", 'width': 4},
                    'thickness': 0.75,
                    'value': 80
                }
            }
        ),
        row=2, col=1
    )
    
    # Summary table
    summary_data = [
        ['Board Dimensions', f"{data['board_x']} x {data['board_y']}"],
        ['Total Boards', f"{data['board_count']}"],
        ['Total Efficiency', f"{data['total_efficiency']:.2f}%"],
        ['Execution Time', f"{data['execution_time']:.3f}s"],
        ['Total Pieces', f"{sum(piece_counts)}"],
        ['Avg Pieces/Board', f"{sum(piece_counts)/len(boards):.1f}"]
    ]
    
    fig.add_trace(
        go.Table(
            header=dict(values=['Metric', 'Value'],
                       fill_color='paleturquoise',
                       align='left'),
            cells=dict(values=[[row[0] for row in summary_data],
                              [row[1] for row in summary_data]],
                      fill_color='lavender',
                      align='left')
        ),
        row=2, col=2
    )
    
    # Update layout
    fig.update_layout(
        title_text="Nesting Results Dashboard",
        showlegend=False,
        height=800,
        width=1200
    )
    
    return fig

def create_comprehensive_dashboard(data):
    """Create a comprehensive single-page dashboard with all boards and analysis."""
    boards = data['boards']
    n_boards = len(boards)
    board_x = data['board_x']
    board_y = data['board_y']

    # Calculate statistics
    efficiencies = [board['efficiency'] for board in boards]
    piece_counts = [board['piece_count'] for board in boards]
    mean_eff = np.mean(efficiencies)
    median_eff = np.median(efficiencies)
    std_eff = np.std(efficiencies)
    best_board_idx = np.argmax(efficiencies)
    worst_board_idx = np.argmin(efficiencies)

    # Format execution time for display
    exec_time = data['execution_time']
    if exec_time >= 60:
        minutes = int(exec_time // 60)
        seconds = int(exec_time % 60)
        exec_time_str = f"{minutes}m {seconds}s"
    else:
        exec_time_str = f"{exec_time:.2f}s"

    # Calculate number of rows needed for boards (3 boards per row)
    boards_rows = (n_boards + 2) // 3  # Ceiling division
    total_rows = boards_rows + 2  # Add 2 rows for charts and statistics

    # Calculate row heights - boards get equal height, last 2 rows get fixed proportions
    board_row_height = 0.52 / boards_rows if boards_rows > 0 else 0
    row_heights = [board_row_height] * boards_rows + [0.22, 0.26]

    # Create subplot titles dynamically
    board_titles = [f'Board {board["board_id"]} - {board["efficiency"]:.1f}%' for board in boards]
    # Pad with empty strings if needed to make grid complete
    board_titles.extend([''] * (boards_rows * 3 - len(board_titles)))
    subplot_titles = board_titles + [
        'Board Efficiency Comparison',
        'Piece Count Distribution',
        'Efficiency vs Piece Count',
        'Total Efficiency Gauge',
        'Statistical Summary',
        'Board Performance Heatmap'
    ]

    # Create specs array - xy type for all plots except indicator and table
    # Note: xy is the standard Plotly subplot type compatible with scatter, bar, heatmap, etc.
    specs = [[{"type": "xy"}] * 3 for _ in range(boards_rows)]
    specs.extend([
        [{"type": "xy"}, {"type": "xy"}, {"type": "xy"}],
        [{"type": "indicator"}, {"type": "table"}, {"type": "xy"}]
    ])

    # Adjust vertical spacing depending on number of board rows
    vertical_spacing = 0.10 if boards_rows <= 2 else max(0.03, 0.12 - 0.01 * (boards_rows - 2))

    fig = make_subplots(
        rows=total_rows,
        cols=3,
        row_heights=row_heights,
        column_widths=[0.33, 0.33, 0.34],
        subplot_titles=subplot_titles,
        specs=specs,
        vertical_spacing=vertical_spacing,
        horizontal_spacing=0.08
    )

    colors = px.colors.qualitative.Set3

    # Add all board visualizations dynamically
    for idx, board in enumerate(boards):
        row = (idx // 3) + 1
        col = (idx % 3) + 1
        if row > boards_rows:  # Skip if we've exceeded the number of board rows
            break

        # Add board boundary
        fig.add_shape(
            type="rect",
            x0=0, y0=0, x1=board_x, y1=board_y,
            line=dict(color="black", width=2),
            fillcolor="rgba(240, 240, 240, 0.2)",
            row=row, col=col
        )

        # Add pieces
        for i, piece in enumerate(board['pieces']):
            coordinates = piece['data']
            if coordinates:
                x_coords = [coord[0] for coord in coordinates]
                y_coords = [coord[1] for coord in coordinates]
                x_coords.append(x_coords[0])
                y_coords.append(y_coords[0])

                color = colors[i % len(colors)]

                fig.add_trace(
                    go.Scatter(
                        x=x_coords,
                        y=y_coords,
                        fill='toself',
                        fillcolor=color,
                        line=dict(color=color, width=1),
                        mode='lines',
                        showlegend=False,
                        name=f'Piece {piece["piece_id"]}',
                        hovertemplate=(
                            f'<b>Board {board["board_id"]}</b><br>' +
                            f'Piece {piece["piece_id"]}<br>' +
                            f'Angle: {piece["angle"]}°<br>' +
                            '<extra></extra>'
                        )
                    ),
                    row=row, col=col
                )

        # Update axes for each board subplot with actual dimension values
        fig.update_xaxes(
            title_text='',
            showgrid=True,
            gridwidth=1,
            gridcolor='lightgray',
            range=[0, board_x],
            tickmode='array',
            tickvals=[0, 500, 1000, 1500, 2000, board_x],
            ticktext=['0', '500', '1000', '1500', '2000', f'{board_x:.0f}'],
            row=row, col=col
        )
        fig.update_yaxes(
            title_text='',
            showgrid=True,
            gridwidth=1,
            gridcolor='lightgray',
            range=[0, board_y],
            tickmode='array',
            tickvals=[0, 250, 500, 750, 1000, board_y],
            ticktext=['0', '250', '500', '750', '1000', f'{board_y:.0f}'],
            scaleanchor=f"x{((row-1)*3 + col)}",
            scaleratio=1,
            row=row, col=col
        )

    # Calculate row positions dynamically based on number of board rows
    charts_row = boards_rows + 1  # Row for bar charts and scatter plot
    stats_row = boards_rows + 2   # Row for gauge, table, and heatmap

    # Charts Row, Col 1: Board Efficiency Comparison Bar Chart
    board_ids = [f"Board {board['board_id']}" for board in boards]
    colors_bar = ['green' if i == best_board_idx else 'red' if i == worst_board_idx else 'steelblue'
                  for i in range(n_boards)]

    fig.add_trace(
        go.Bar(
            x=board_ids,
            y=efficiencies,
            text=[f"{eff:.1f}%" for eff in efficiencies],
            textposition='outside',
            marker=dict(color=colors_bar, line=dict(color='black', width=1)),
            name='Efficiency',
            hovertemplate='%{x}<br>Efficiency: %{y:.2f}%<extra></extra>'
        ),
        row=charts_row, col=1
    )

    fig.update_xaxes(title_text='Board', row=charts_row, col=1)
    fig.update_yaxes(title_text='Efficiency (%)', range=[0, 100], row=charts_row, col=1)

    # Charts Row, Col 2: Piece Count Bar Chart
    fig.add_trace(
        go.Bar(
            x=board_ids,
            y=piece_counts,
            text=piece_counts,
            textposition='outside',
            marker=dict(color='coral', line=dict(color='black', width=1)),
            name='Piece Count',
            hovertemplate='%{x}<br>Pieces: %{y}<extra></extra>'
        ),
        row=charts_row, col=2
    )

    fig.update_xaxes(title_text='Board', row=charts_row, col=2)
    fig.update_yaxes(title_text='Number of Pieces', row=charts_row, col=2)

    # Charts Row, Col 3: Efficiency vs Piece Count Scatter Plot
    fig.add_trace(
        go.Scatter(
            x=piece_counts,
            y=efficiencies,
            mode='markers+text',
            text=board_ids,
            textposition='top center',
            marker=dict(
                size=15,
                color=efficiencies,
                colorscale='RdYlGn',
                showscale=True,
                colorbar=dict(
                    title="Efficiency<br>(%)",
                    x=1.15,
                    len=0.2,
                    y=0.375
                ),
                line=dict(color='black', width=1)
            ),
            name='Boards',
            hovertemplate='%{text}<br>Pieces: %{x}<br>Efficiency: %{y:.2f}%<extra></extra>'
        ),
        row=charts_row, col=3
    )

    fig.update_xaxes(title_text='Number of Pieces', row=charts_row, col=3)
    fig.update_yaxes(title_text='Efficiency (%)', range=[0, 100], row=charts_row, col=3)

    # Stats Row, Col 1: Total Efficiency Gauge
    fig.add_trace(
        go.Indicator(
            mode="gauge+number+delta",
            value=data['total_efficiency'],
            domain={'x': [0, 1], 'y': [0, 1]},
            title={'text': "Overall<br>Efficiency (%)", 'font': {'size': 14}},
            delta={'reference': 60, 'valueformat': '.2f'},
            number={'suffix': '%', 'font': {'size': 24}},
            gauge={
                'axis': {'range': [None, 100], 'tickwidth': 1, 'tickcolor': "darkgray"},
                'bar': {'color': "darkgreen"},
                'bgcolor': "white",
                'borderwidth': 2,
                'bordercolor': "gray",
                'steps': [
                    {'range': [0, 40], 'color': 'lightcoral'},
                    {'range': [40, 60], 'color': 'lightyellow'},
                    {'range': [60, 80], 'color': 'lightblue'},
                    {'range': [80, 100], 'color': 'lightgreen'}
                ],
                'threshold': {
                    'line': {'color': "red", 'width': 4},
                    'thickness': 0.75,
                    'value': 70
                }
            }
        ),
        row=stats_row, col=1
    )

    # Stats Row, Col 2: Statistical Summary Table
    summary_data = [
        ['Board Dimensions', f"{board_x} x {board_y} mm"],
        ['Total Boards', f"{n_boards}"],
        ['Total Pieces', f"{sum(piece_counts)}"],
        ['Avg Pieces/Board', f"{sum(piece_counts)/n_boards:.1f}"],
        ['Mean Efficiency', f"{mean_eff:.2f}%"],
        ['Median Efficiency', f"{median_eff:.2f}%"],
        ['Std Dev Efficiency', f"{std_eff:.2f}%"],
        ['Best Board', f"Board {boards[best_board_idx]['board_id']} ({efficiencies[best_board_idx]:.1f}%)"],
        ['Worst Board', f"Board {boards[worst_board_idx]['board_id']} ({efficiencies[worst_board_idx]:.1f}%)"],
        ['Total Efficiency', f"{data['total_efficiency']:.2f}%"],
        ['Execution Time', exec_time_str]
    ]

    fig.add_trace(
        go.Table(
            header=dict(
                values=['<b>Metric</b>', '<b>Value</b>'],
                fill_color='paleturquoise',
                align='left',
                font=dict(size=12, color='black')
            ),
            cells=dict(
                values=[[row[0] for row in summary_data],
                        [row[1] for row in summary_data]],
                fill_color='lavender',
                align='left',
                font=dict(size=11)
            )
        ),
        row=stats_row, col=2
    )

    # Stats Row, Col 3: Board Performance Heatmap
    # Create a matrix for heatmap: rows = boards, cols = [efficiency, pieces]
    heatmap_data = np.array([[eff, pc] for eff, pc in zip(efficiencies, piece_counts)])

    # Normalize to 0-100 scale for better visualization
    heatmap_normalized = np.zeros_like(heatmap_data, dtype=float)
    heatmap_normalized[:, 0] = heatmap_data[:, 0]  # Efficiency already 0-100
    heatmap_normalized[:, 1] = (heatmap_data[:, 1] / max(piece_counts)) * 100  # Normalize pieces

    fig.add_trace(
        go.Heatmap(
            z=heatmap_normalized,
            x=['Efficiency (%)', 'Pieces (norm)'],
            y=board_ids,
            colorscale='RdYlGn',
            text=[[f"{efficiencies[i]:.1f}%", f"{piece_counts[i]}"]
                  for i in range(n_boards)],
            texttemplate='%{text}',
            textfont={"size": 10},
            showscale=False,
            hovertemplate='%{y}<br>%{x}: %{text}<extra></extra>'
        ),
        row=stats_row, col=3
    )

    # Update main layout for responsive desktop viewing
    # Calculate dynamic height based on number of board rows
    total_height = max(1000, 300 * boards_rows + 700)  # Base height + additional height per row of boards

    fig.update_layout(
        title={
            'text': f"<b>Comprehensive Nesting Results Dashboard</b><br>" +
                    f"<sup>Total Efficiency: {data['total_efficiency']:.2f}% | " +
                    f"Boards: {n_boards} | Total Pieces: {sum(piece_counts)} | " +
                    f"Board Size: {board_x} x {board_y} mm | " +
                    f"Execution Time: {exec_time_str}</sup>",
            'x': 0.5,
            'xanchor': 'center',
            'font': {'size': 18}
        },
        showlegend=False,
        autosize=True,
        height=total_height,
        plot_bgcolor='white',
        paper_bgcolor='whitesmoke',
        margin=dict(l=40, r=40, t=80, b=30)
    )

    return fig


def main():
    """Main function to create and display all visualizations."""
    # Load data
    data = load_nesting_data('genetic_nesting_optimized_result.json')

    print("Creating comprehensive nesting dashboard...")
    print(f"Total boards: {data['board_count']}")
    print(f"Overall efficiency: {data['total_efficiency']:.2f}%")
    print(f"Board dimensions: {data['board_x']} x {data['board_y']}")

    # Create comprehensive dashboard
    dashboard = create_comprehensive_dashboard(data)

    # Save dashboard to HTML file (better for WSL environments)
    output_file = 'nesting_dashboard.html'
    dashboard.write_html(output_file)

    print("\n" + "="*60)
    print("Dashboard created successfully!")
    print("="*60)
    print(f"\nHTML file saved to: {output_file}")
    print(f"Full path: /mnt/c/Users/bruno/Desktop/ncc-plotly/{output_file}")
    print("\nTo view the dashboard:")
    print("  1. Navigate to: C:\\Users\\bruno\\Desktop\\ncc-plotly\\")
    print(f"  2. Open '{output_file}' in your web browser")
    print("  3. Or double-click the file to open it automatically")
    print("\n" + "="*60)

if __name__ == "__main__":
    main()