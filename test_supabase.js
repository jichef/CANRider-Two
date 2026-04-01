
const { createClient } = require('@supabase/supabase-js');

const supabaseUrl = 'https://jmisxaxqwtkudvkytkha.supabase.co';
const supabaseKey = 'eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6ImptaXN4YXhxd3RrdWR2a3l0a2hhIiwicm9sZSI6InNlcnZpY2Vfcm9sZSIsImlhdCI6MTc3MDQyMDExMCwiZXhwIjoyMDg1OTk2MTEwfQ.lnKW5cbMC4q4XDunTvI2KSI0OLvxTPJC-xH1ZfaBAJU';
const supabase = createClient(supabaseUrl, supabaseKey);

async function checkColumnsWithEmptySelect() {
  console.log('Intentando deducir columnas mediante error select...');
  
  // Si pedimos una columna que NO existe, a veces el error nos dice cuáles SI existen
  const { data, error } = await supabase
    .from('locations')
    .select('id, name, latitude, longitude, created_at, updated_at');

  if (error) {
    console.error('Error:', error.message);
  } else {
    console.log('¡Éxito en select!', data);
  }
}

checkColumnsWithEmptySelect();
