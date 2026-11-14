import { createRoot } from 'react-dom/client';
import App from './App';
import { connect } from './bridge';
import './styles.css';

createRoot(document.getElementById('root')!).render(<App />);
void connect();
