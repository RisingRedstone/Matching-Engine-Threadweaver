import pandas as pd
import seaborn as sns
import matplotlib.pyplot as plt
import plotly.express as px
import io

# 1. Load the data
# (Using your provided sample data. Replace this block with pd.read_csv('your_file.csv') for your full dataset)
# csv_data = """Approach,Number of Writers,Buffer Size,Numbers to write,Time elapsed
# Three-Pointer,4,524288,262144,0.028916196
# Three-Pointer,4,524288,262144,0.026423639
# Three-Pointer,4,524288,262144,0.026513536
# Cell-Lockable,8,16777216,16777216,5.181461169
# Cell-Lockable,8,16777216,16777216,5.277433695
# Cell-Lockable,8,16777216,33554432,10.429666290
# Cell-Lockable,8,16777216,33554432,10.610263483"""

df = pd.read_csv('output2.csv')

# Read data into a pandas DataFrame
# df = pd.read_csv(io.StringIO(csv_data))

# Clean up column names to avoid matching errors
df.columns = df.columns.str.strip()
df['Buffer Size'] = df['Buffer Size'].astype(str)

# !!! CRITICAL FIX 1: Force X and Y to be actual numbers so lines can connect them
df['Numbers to write'] = pd.to_numeric(df['Numbers to write'], errors='coerce')
df['Time elapsed'] = pd.to_numeric(df['Time elapsed'], errors='coerce')

# !!! CRITICAL FIX 2: Define strict category orders so the subplots match perfectly
cat_orders = {
    'Number of Writers': sorted(df['Number of Writers'].unique()),
    'Buffer Size': sorted(df['Buffer Size'].unique()),
    'Approach': sorted(df['Approach'].unique())
}

# 2. Calculate the means & sort mathematically
df_means = df.groupby(
    ['Numbers to write', 'Number of Writers', 'Buffer Size', 'Approach'], 
    as_index=False
).agg(
    Time_elapsed_mean=('Time elapsed', 'mean'),
    Time_elapsed_std=('Time elapsed', 'std') # Calculates the spread
)

# Rename the mean column back to 'Time elapsed' so the plot recognizes it
df_means = df_means.rename(columns={'Time_elapsed_mean': 'Time elapsed'})

# Sort by the actual numeric values so the line doesn't zig-zag
df_means = df_means.sort_values(by=['Number of Writers', 'Buffer Size', 'Approach', 'Numbers to write'])

# 3. Create the base scatter plot (pass in category_orders)
fig = px.scatter(
    df,
    x='Numbers to write',
    y='Time elapsed',
    facet_col='Number of Writers',
    color='Buffer Size',
    symbol='Approach',
    hover_data=df.columns,
    log_x=True,
    log_y=True,
    category_orders=cat_orders, # <--- Added this
    title="Performance Comparison: Individual Runs & Trends (Means)",
    labels={
        "Numbers to write": "Numbers to Write",
        "Time elapsed": "Time Elapsed (seconds)"
    }
)

# 4. Create the mean lines (pass in the exact same category_orders)
fig_lines = px.line(
    df_means,
    x='Numbers to write',
    y='Time elapsed',
    error_y='Time_elapsed_std',  # <--- NEW: This adds the vertical spread bars
    facet_col='Number of Writers',
    color='Buffer Size',
    symbol='Approach',
    log_x=True,
    log_y=True,
    category_orders=cat_orders
)

# 5. Add the lines to the original scatter
for trace in fig_lines.data:
    trace.update(showlegend=False) 
    fig.add_trace(trace)

# 6. Customize the look
# Make dots transparent and lines thick
fig.update_traces(marker=dict(size=10, opacity=0.4), selector=dict(mode='markers'))
fig.update_traces(line=dict(width=4), selector=dict(type='scatter', mode='lines'))

# Allow Y-axis to scale independently
fig.update_yaxes(matches=None) 

# Save
output_file = 'interactive_approach_scatter_with_means.html'
fig.write_html(output_file)
