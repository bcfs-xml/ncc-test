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
        specs=[[{"type": "bar"}, {"type": "bar"}],
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

def main():
    """Main function to create and display all visualizations."""
    # Load data
    data = load_nesting_data('nesting_results.json')
    
    print("Creating nesting visualizations...")
    print(f"Total boards: {data['board_count']}")
    print(f"Overall efficiency: {data['total_efficiency']:.2f}%")
    print(f"Board dimensions: {data['board_x']} x {data['board_y']}")
    
    # Create dashboard
    dashboard = create_efficiency_dashboard(data)
    dashboard.show()
    
    # Create individual board visualizations
    for board in data['boards']:
        print(f"\nProcessing Board {board['board_id']}")
        print(f"  - Efficiency: {board['efficiency']:.1f}%")
        print(f"  - Pieces: {board['piece_count']}")
        
        fig = create_board_visualization(board, data['board_x'], data['board_y'])
        fig.show()
    
    # Create a combined overview with all boards in subplots
    n_boards = len(data['boards'])
    cols = min(3, n_boards)  # Maximum 3 columns
    rows = (n_boards + cols - 1) // cols  # Calculate rows needed
    
    combined_fig = make_subplots(
        rows=rows, cols=cols,
        subplot_titles=[f'Board {board["board_id"]} ({board["efficiency"]:.1f}%)' 
                       for board in data['boards']],
        specs=[[{"type": "scatter"}] * cols for _ in range(rows)]
    )
    
    colors = px.colors.qualitative.Set1
    
    for idx, board in enumerate(data['boards']):
        row = idx // cols + 1
        col = idx % cols + 1
        
        # Add board boundary
        combined_fig.add_shape(
            type="rect",
            x0=0, y0=0, x1=data['board_x'], y1=data['board_y'],
            line=dict(color="gray", width=1),
            fillcolor="rgba(240, 240, 240, 0.1)",
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
                
                combined_fig.add_trace(
                    go.Scatter(
                        x=x_coords,
                        y=y_coords,
                        fill='tonext',
                        fillcolor=color,
                        line=dict(color=color, width=1),
                        mode='lines',
                        showlegend=False,
                        name=f'Piece {piece["piece_id"]}'
                    ),
                    row=row, col=col
                )
    
    # Update combined layout
    combined_fig.update_layout(
        title_text=f"All Boards Overview - Total Efficiency: {data['total_efficiency']:.1f}%",
        height=300 * rows,
        width=400 * cols,
        showlegend=False
    )
    
    # Make all subplots have equal aspect ratio
    for i in range(1, rows + 1):
        for j in range(1, cols + 1):
            combined_fig.update_xaxes(
                showgrid=True,
                gridwidth=1,
                gridcolor='lightgray',
                range=[-50, data['board_x'] + 50],
                row=i, col=j
            )
            combined_fig.update_yaxes(
                showgrid=True,
                gridwidth=1,
                gridcolor='lightgray',
                range=[-50, data['board_y'] + 50],
                scaleanchor=f"x{((i-1)*cols + j)}",
                scaleratio=1,
                row=i, col=j
            )
    
    combined_fig.show()
    
    print("\nVisualization complete! All charts have been displayed.")

if __name__ == "__main__":
    main()